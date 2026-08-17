#include "amf_ue_id_index_store.hpp"

namespace amf {

namespace {

std::string index_key(unsigned long amf_ue_id) {
    return "amf:ueidindex:" + std::to_string(amf_ue_id);
}

} // namespace

void AmfUeIdIndexStore::put(unsigned long amf_ue_id, std::uint32_t tmsi) {
    redis_->set(index_key(amf_ue_id), std::to_string(tmsi));
}

std::optional<std::uint32_t> AmfUeIdIndexStore::get(unsigned long amf_ue_id) {
    const auto value = redis_->get(index_key(amf_ue_id));
    if (!value) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::stoul(*value));
}

void AmfUeIdIndexStore::remove(unsigned long amf_ue_id) {
    redis_->del(index_key(amf_ue_id));
}

} // namespace amf
