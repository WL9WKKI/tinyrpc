#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace diag {

// 取消标志注册表（thread-per-task 协作式取消）
class CancelManager {
public:
    std::shared_ptr<std::atomic<bool>> registerTask(uint64_t task_id);
    void cancel(uint64_t task_id);
    void unregister(uint64_t task_id);

private:
    std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<std::atomic<bool>>> flags_;
};

}  // namespace diag
