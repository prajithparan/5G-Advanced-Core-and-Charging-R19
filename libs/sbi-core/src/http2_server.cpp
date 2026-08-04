#include "sbi_core/http2_server.hpp"

#include <boost/asio/write.hpp>
#include <nghttp2/nghttp2.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <unordered_map>

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

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(boost::asio::ip::tcp::socket socket, const std::vector<Route>& routes)
        : socket_(std::move(socket)), routes_(routes) {}

    ~Connection() {
        if (session_ != nullptr) {
            nghttp2_session_del(session_);
        }
    }

    void start() {
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

private:
    void close() {
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
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

    static nghttp2_nv make_nv(const std::string& name, const std::string& value) {
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

    boost::asio::ip::tcp::socket socket_;
    const std::vector<Route>& routes_;
    nghttp2_session* session_ = nullptr;
    std::array<std::uint8_t, 65536> read_buf_{};
    std::vector<std::uint8_t> write_buf_;
    std::unordered_map<std::int32_t, StreamContext> streams_;
};

} // namespace

class Server::Impl {
public:
    Impl(boost::asio::io_context& ioc, std::string address, unsigned short port)
        : ioc_(ioc),
          acceptor_(ioc,
                    boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(address), port)) {}

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
                    auto conn = std::make_shared<Connection>(std::move(socket), routes_);
                    conn->start();
                }
                do_accept();
            });
    }

    boost::asio::io_context& ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<Route> routes_;
};

Server::Server(boost::asio::io_context& ioc, std::string address, unsigned short port)
    : impl_(std::make_unique<Impl>(ioc, std::move(address), port)) {}

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
