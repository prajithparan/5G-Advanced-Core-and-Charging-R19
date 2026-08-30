#include "sbi_core/io_context_pool.hpp"

#include <algorithm>
#include <thread>
#include <vector>

namespace sbi_core {

void run_multi_threaded(boost::asio::io_context& ioc) {
    const unsigned int worker_count = std::max(2u, std::thread::hardware_concurrency());

    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1);
    for (unsigned int i = 1; i < worker_count; ++i) {
        workers.emplace_back([&ioc] { ioc.run(); });
    }

    ioc.run(); // calling thread participates too, instead of just waiting on the others

    for (auto& worker : workers) {
        worker.join();
    }
}

} // namespace sbi_core
