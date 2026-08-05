#include "sbi_core/json_body.hpp"

namespace sbi_core::http2 {

Response problem_response(int status, const std::string& title, const std::string& detail) {
    auto pd = sbi_core::make_problem_details(status, title, detail);
    nlohmann::json j = pd;
    Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/problem+json");
    r.body = j.dump();
    return r;
}

} // namespace sbi_core::http2
