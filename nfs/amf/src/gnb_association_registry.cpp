#include "gnb_association_registry.hpp"

namespace amf::ngap {

namespace {
std::string key_of(const std::vector<std::uint8_t>& gnb_id) {
    return std::string(gnb_id.begin(), gnb_id.end());
}
} // namespace

void GnbAssociationRegistry::register_gnb(const std::vector<std::uint8_t>& gnb_id,
                                          ngap_core::SctpSocket* socket) {
    auto assoc = std::make_shared<GnbAssociation>();
    assoc->socket = socket;
    std::lock_guard<std::mutex> lock(registry_mutex_);
    associations_[key_of(gnb_id)] = std::move(assoc);
}

void GnbAssociationRegistry::unregister_gnb(const std::vector<std::uint8_t>& gnb_id) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    associations_.erase(key_of(gnb_id));
}

std::shared_ptr<GnbAssociation>
GnbAssociationRegistry::find(const std::vector<std::uint8_t>& gnb_id) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    const auto it = associations_.find(key_of(gnb_id));
    if (it == associations_.end()) {
        return nullptr;
    }
    return it->second;
}

std::optional<std::vector<std::uint8_t>>
GnbAssociationRegistry::send_and_await_reply(const std::vector<std::uint8_t>& gnb_id,
                                             const std::vector<std::uint8_t>& pdu_bytes,
                                             std::chrono::milliseconds timeout) {
    auto assoc = find(gnb_id);
    if (assoc == nullptr) {
        return std::nullopt;
    }

    {
        std::lock_guard<std::mutex> lock(assoc->reply_mutex);
        assoc->awaiting_reply = true;
        assoc->reply_bytes.reset();
    }

    {
        std::lock_guard<std::mutex> send_lock(assoc->send_mutex);
        assoc->socket->send(pdu_bytes);
    }

    std::unique_lock<std::mutex> lock(assoc->reply_mutex);
    const bool got_reply =
        assoc->reply_cv.wait_for(lock, timeout, [&] { return assoc->reply_bytes.has_value(); });
    assoc->awaiting_reply = false;
    if (!got_reply) {
        return std::nullopt;
    }
    return std::move(*assoc->reply_bytes);
}

bool GnbAssociationRegistry::deliver_reply_if_pending(const std::vector<std::uint8_t>& gnb_id,
                                                      std::vector<std::uint8_t> reply_bytes) {
    auto assoc = find(gnb_id);
    if (assoc == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(assoc->reply_mutex);
    if (!assoc->awaiting_reply) {
        return false;
    }
    assoc->reply_bytes = std::move(reply_bytes);
    assoc->reply_cv.notify_one();
    return true;
}

bool GnbAssociationRegistry::send(const std::vector<std::uint8_t>& gnb_id,
                                  const std::vector<std::uint8_t>& pdu_bytes) {
    auto assoc = find(gnb_id);
    if (assoc == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> send_lock(assoc->send_mutex);
    assoc->socket->send(pdu_bytes);
    return true;
}

} // namespace amf::ngap
