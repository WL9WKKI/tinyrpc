#include "agent/cancel_manager.h"

namespace diag {

std::shared_ptr<std::atomic<bool>> CancelManager::registerTask(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& f = flags_[task_id];
    if (!f) f = std::make_shared<std::atomic<bool>>(false);
    return f;
}

void CancelManager::cancel(uint64_t task_id) {
    std::shared_ptr<std::atomic<bool>> f;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = flags_[task_id];
        if (!slot) slot = std::make_shared<std::atomic<bool>>(false);
        f = slot;
    }
    f->store(true);
}

void CancelManager::unregister(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    flags_.erase(task_id);
}

}  // namespace diag
