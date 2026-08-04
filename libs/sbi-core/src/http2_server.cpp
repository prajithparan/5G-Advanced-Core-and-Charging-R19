#include "sbi_core/http2_server.hpp"

#include <boost/asio/ssl.hpp>
#include <boost/asio/write.hpp>
#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace {

// The only protocol this server will ever negotiate -- see TlsConfig's doc comment: no h2c
// fallback, no HTTP/1.1. Wire format for SSL_select_next_proto: 1-byte length prefix + bytes.
constexpr unsigned char kAlpnH2Wire[] = {2, 'h', '2'};

int alpn_select_callback(SSL* /*ssl*/,
                         const unsigned char** out,
                         unsigned char* outlen,
                         const unsigned char* in,
                         unsigned int inlen,
                         void* /*arg*/) {
    if (SSL_select_next_proto(const_cast<unsigned char**>(out),
                              outlen,
                              kAlpnH2Wire,
                              sizeof(kAlpnH2Wire),
                              in,
                              inlen) != OPENSSL_NPN_NEGOTIATED) {
        // Client didn't offer "h2" -- reject the handshake rather than fall back to HTTP/1.1 or
        // proceed with no negotiated protocol. See TlsConfig's doc comment: h2 is not optional.
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    return SSL_TLSEXT_ERR_OK;
}

} // namespace

namespace sbi_core::http2 {

namespace {

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> segments;
    std::stringstream ss(path);
    std::string segment;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) {
            segments.push_back(segment);
        }
    }
    return segments;
}

struct Route {
    std::string method;
    std::vector<std::string> pattern_segments;
    Handler handler;
};

bool try_match(const Route& route,
               const std::string& method,
               const std::vector<std::string>& path_segments,
               std::map<std::string, std::string>& params_out) {
    if (route.method != method) {
        return false;
    }
    if (route.pattern_segments.size() != path_segments.size()) {
        return false;
    }
    std::map<std::string, std::string> params;
    for (std::size_t i = 0; i < route.pattern_segments.size(); ++i) {
        const auto& pat = route.pattern_segments[i];
        if (pat.size() >= 2 && pat.front() == '{' && pat.back() == '}') {
            params.emplace(pat.substr(1, pat.size() - 2), path_segments[i]);
        } else if (pat != path_segments[i]) {
            return false;
        }
    }
    params_out = std::move(params);
    return true;
}

} // namespace

Response Response::json(int status, std::string body_json) {
    Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/json");
    r.body = std::move(body_json);
    return r;
}

namespace {

struct StreamContext {
    std::string method;
    std::string path;
    std::multimap<std::string, std::string> headers;
    std::string body;
    std::string response_body;
    std::size_t response_offset = 0;
};

using SslStream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(boost::asio::ip::tcp::socket socket,
               boost::asio::ssl::context& ssl_ctx,
               const std::vector<Route>& routes)
        : socket_(std::move(socket), ssl_ctx), routes_(routes) {}

    ~Connection() {
        if (session_ != nullptr) {
            nghttp2_session_del(session_);
        }
    }

    void start() {
        auto self = shared_from_this();
        socket_.async_handshake(
            boost::asio::ssl::stream_base::server, [self](boost::system::error_code ec) {
                if (ec) {
                    spdlog::warn("sbi-core: TLS handshake failed: {}", ec.message());
                    self->close();
                    return;
                }
                self->on_handshake_complete();
            });
    }

private:
    void on_handshake_complete() {
        nghttp2_session_callbacks* callbacks = nullptr;
        nghttp2_session_callbacks_new(&callbacks);
        nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
                                                                &Connection::on_begin_headers);
        nghttp2_session_callbacks_set_on_header_callback(callbacks, &Connection::on_header);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, &Connection::on_frame_recv);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
                                                                  &Connection::on_data_chunk_recv);
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
                                                               &Connection::on_stream_close);

        nghttp2_session_server_new(&session_, callbacks, this);
        nghttp2_session_callbacks_del(callbacks);

        std::array<nghttp2_settings_entry, 1> iv{{{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 128}}};
        nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv.data(), iv.size());

        do_write();
    }

    // No TLS close_notify is sent -- just an abrupt close of the underlying TCP socket. A fully
    // correct async_shutdown (sending close_notify and waiting for the peer's, without blocking
    // the io_context thread) would need its own timeout handling to avoid an unresponsive peer
    // leaking the Connection indefinitely; skipped here as a disclosed simplification (see
    // docs/DECISIONS.md ADR-0011) rather than half-implemented. Every close() call site here is
    // already a teardown/error path, not graceful application-level connection reuse handoff.
    void close() {
        boost::system::error_code ec;
        socket_.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.lowest_layer().close(ec);
    }

    void do_read() {
        auto self = shared_from_this();
        socket_.async_read_some(
            boost::asio::buffer(read_buf_), [self](boost::system::error_code ec, std::size_t n) {
                if (ec) {
                    self->close();
                    return;
                }
                auto rv = nghttp2_session_mem_recv(self->session_, self->read_buf_.data(), n);
                if (rv < 0) {
                    spdlog::warn("sbi-core: nghttp2_session_mem_recv failed: {}",
                                 nghttp2_strerror(static_cast<int>(rv)));
                    self->close();
                    return;
                }
                self->do_write();
            });
    }

    void do_write() {
        write_buf_.clear();
        for (;;) {
            const std::uint8_t* data_ptr = nullptr;
            const auto len = nghttp2_session_mem_send(session_, &data_ptr);
            if (len <= 0) {
                break;
            }
            write_buf_.insert(write_buf_.end(), data_ptr, data_ptr + len);
        }

        if (write_buf_.empty()) {
            if (nghttp2_session_want_read(session_) != 0) {
                do_read();
            } else {
                close();
            }
            return;
        }

        auto self = shared_from_this();
        boost::asio::async_write(socket_,
                                 boost::asio::buffer(write_buf_),
                                 [self](boost::system::error_code ec, std::size_t /*n*/) {
                                     if (ec) {
                                         self->close();
                                         return;
                                     }
                                     if (nghttp2_session_want_read(self->session_) != 0) {
                                         self->do_read();
                                     } else if (nghttp2_session_want_write(self->session_) == 0) {
                                         self->close();
                                     }
                                 });
    }

    void handle_request_complete(std::int32_t stream_id) {
        auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            return;
        }
        StreamContext& ctx = it->second;

        Request req;
        req.method = ctx.method;
        req.path = ctx.path;
        req.headers = ctx.headers;
        req.body = ctx.body;

        const auto path_only = ctx.path.substr(0, ctx.path.find('?'));
        const auto segments = split_path(path_only);

        Response resp;
        bool matched = false;
        for (const auto& route : routes_) {
            std::map<std::string, std::string> params;
            if (try_match(route, req.method, segments, params)) {
                req.path_params = std::move(params);
                resp = route.handler(req);
                matched = true;
                break;
            }
        }
        if (!matched) {
            resp.status = 404;
            resp.headers.emplace("content-type", "application/problem+json");
            resp.body =
                R"({"status":404,"title":"Not Found","detail":"No route matches this method/path"})";
        }

        ctx.response_body = std::move(resp.body);
        ctx.response_offset = 0;

        std::vector<std::string> status_str_storage;
        status_str_storage.push_back(std::to_string(resp.status));

        std::vector<nghttp2_nv> nva;
        nva.push_back(make_nv(":status", status_str_storage.back()));
        for (const auto& [name, value] : resp.headers) {
            nva.push_back(make_nv(name, value));
        }

        nghttp2_data_provider data_prd{};
        data_prd.read_callback = &Connection::on_read_response_body;

        nghttp2_submit_response(session_,
                                stream_id,
                                nva.data(),
                                nva.size(),
                                ctx.response_body.empty() ? nullptr : &data_prd);
    }

    // string_view, not const std::string&: a const-ref parameter bound to a string literal (e.g.
    // make_nv(":status", ...)) would materialize a temporary std::string that's destroyed at the
    // end of the full expression, leaving the returned nghttp2_nv's pointer dangling into freed
    // stack memory by the time nghttp2_submit_response's caller actually reads it. ASan caught this
    // in CI (stack-use-after-scope, ":status" is 7 bytes -- matched the reported read size
    // exactly). string_view has no such trap: it just wraps whatever storage the caller already
    // owns (a literal's static storage, or resp.headers'/status_str_storage's buffers).
    static nghttp2_nv make_nv(std::string_view name, std::string_view value) {
        nghttp2_nv nv{};
        nv.name = reinterpret_cast<std::uint8_t*>(const_cast<char*>(name.data()));
        nv.value = reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data()));
        nv.namelen = name.size();
        nv.valuelen = value.size();
        nv.flags = NGHTTP2_NV_FLAG_NONE;
        return nv;
    }

    static ssize_t on_read_response_body(nghttp2_session* /*session*/,
                                         std::int32_t stream_id,
                                         std::uint8_t* buf,
                                         std::size_t length,
                                         std::uint32_t* data_flags,
                                         nghttp2_data_source* /*source*/,
                                         void* user_data) {
        auto* self = static_cast<Connection*>(user_data);
        auto it = self->streams_.find(stream_id);
        if (it == self->streams_.end()) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            return 0;
        }
        StreamContext& ctx = it->second;
        const std::size_t remaining = ctx.response_body.size() - ctx.response_offset;
        const std::size_t n = std::min(remaining, length);
        std::memcpy(buf, ctx.response_body.data() + ctx.response_offset, n);
        ctx.response_offset += n;
        if (ctx.response_offset >= ctx.response_body.size()) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        }
        return static_cast<ssize_t>(n);
    }

    static int
    on_begin_headers(nghttp2_session* /*session*/, const nghttp2_frame* frame, void* user_data) {
        if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }
        auto* self = static_cast<Connection*>(user_data);
        self->streams_.emplace(frame->hd.stream_id, StreamContext{});
        return 0;
    }

    static int on_header(nghttp2_session* /*session*/,
                         const nghttp2_frame* frame,
                         const std::uint8_t* name,
                         std::size_t namelen,
                         const std::uint8_t* value,
                         std::size_t valuelen,
                         std::uint8_t /*flags*/,
                         void* user_data) {
        if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }
        auto* self = static_cast<Connection*>(user_data);
        auto it = self->streams_.find(frame->hd.stream_id);
        if (it == self->streams_.end()) {
            return 0;
        }
        const std::string name_str(reinterpret_cast<const char*>(name), namelen);
        const std::string value_str(reinterpret_cast<const char*>(value), valuelen);
        if (name_str == ":method") {
            it->second.method = value_str;
        } else if (name_str == ":path") {
            it->second.path = value_str;
        } else if (!name_str.empty() && name_str.front() != ':') {
            it->second.headers.emplace(name_str, value_str);
        }
        return 0;
    }

    static int on_data_chunk_recv(nghttp2_session* /*session*/,
                                  std::uint8_t /*flags*/,
                                  std::int32_t stream_id,
                                  const std::uint8_t* data,
                                  std::size_t len,
                                  void* user_data) {
        auto* self = static_cast<Connection*>(user_data);
        auto it = self->streams_.find(stream_id);
        if (it != self->streams_.end()) {
            it->second.body.append(reinterpret_cast<const char*>(data), len);
        }
        return 0;
    }

    static int
    on_frame_recv(nghttp2_session* /*session*/, const nghttp2_frame* frame, void* user_data) {
        auto* self = static_cast<Connection*>(user_data);
        const bool end_stream = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0;
        if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_REQUEST &&
            end_stream) {
            self->handle_request_complete(frame->hd.stream_id);
        } else if (frame->hd.type == NGHTTP2_DATA && end_stream) {
            self->handle_request_complete(frame->hd.stream_id);
        }
        return 0;
    }

    static int on_stream_close(nghttp2_session* /*session*/,
                               std::int32_t stream_id,
                               std::uint32_t /*error_code*/,
                               void* user_data) {
        auto* self = static_cast<Connection*>(user_data);
        self->streams_.erase(stream_id);
        return 0;
    }

    SslStream socket_;
    const std::vector<Route>& routes_;
    nghttp2_session* session_ = nullptr;
    std::array<std::uint8_t, 65536> read_buf_{};
    std::vector<std::uint8_t> write_buf_;
    std::unordered_map<std::int32_t, StreamContext> streams_;
};

} // namespace

namespace {

boost::asio::ssl::context make_server_ssl_context(const TlsConfig& tls) {
    // tlsv13_server, not the generic tlsv13: restricts the negotiable method set to TLS 1.3 server
    // mode specifically. Combined with no SSLv3/TLS1.0-1.2 context ever being constructed, there is
    // no downgrade path -- this is enforced by which OpenSSL method table gets selected, not just a
    // set_options() flag that could be forgotten.
    boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv13_server);

    try {
        ctx.use_certificate_chain_file(tls.cert_path);
        ctx.use_private_key_file(tls.key_path, boost::asio::ssl::context::pem);
        ctx.load_verify_file(tls.ca_path);
    } catch (const boost::system::system_error& e) {
        throw std::runtime_error("sbi-core: failed to load TLS material (cert=" + tls.cert_path +
                                 ", key=" + tls.key_path + ", ca=" + tls.ca_path +
                                 "): " + e.what());
    }

    // mTLS: require a client certificate and verify it against the CA above. No anonymous/
    // unauthenticated clients accepted -- see docs/DECISIONS.md ADR-0011.
    ctx.set_verify_mode(boost::asio::ssl::verify_peer |
                        boost::asio::ssl::verify_fail_if_no_peer_cert);

    SSL_CTX_set_alpn_select_cb(ctx.native_handle(), alpn_select_callback, nullptr);

    return ctx;
}

} // namespace

class Server::Impl {
public:
    Impl(boost::asio::io_context& ioc, std::string address, unsigned short port, TlsConfig tls)
        : ioc_(ioc),
          acceptor_(ioc,
                    boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(address), port)),
          ssl_ctx_(make_server_ssl_context(tls)) {}

    void add_route(std::string method, std::string path_pattern, Handler handler) {
        std::transform(method.begin(), method.end(), method.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        routes_.push_back(Route{std::move(method), split_path(path_pattern), std::move(handler)});
    }

    void start() { do_accept(); }

    unsigned short local_port() const { return acceptor_.local_endpoint().port(); }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
                if (!ec) {
                    auto conn = std::make_shared<Connection>(std::move(socket), ssl_ctx_, routes_);
                    conn->start();
                }
                do_accept();
            });
    }

    boost::asio::io_context& ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ssl::context ssl_ctx_;
    std::vector<Route> routes_;
};

Server::Server(boost::asio::io_context& ioc,
               std::string address,
               unsigned short port,
               TlsConfig tls)
    : impl_(std::make_unique<Impl>(ioc, std::move(address), port, std::move(tls))) {}

Server::~Server() = default;

void Server::add_route(std::string method, std::string path_pattern, Handler handler) {
    impl_->add_route(std::move(method), std::move(path_pattern), std::move(handler));
}

void Server::start() {
    impl_->start();
}

unsigned short Server::local_port() const {
    return impl_->local_port();
}

} // namespace sbi_core::http2
