#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <json/json.h>

#include "executor/tool_executor.h"
#include "mq/mq_client.h"

namespace diag {

// 慢任务分发器：生产者 + 结果等待（后台线程消费 slow-results）
class SlowTaskDispatcher {
public:
    SlowTaskDispatcher(std::shared_ptr<IMessageQueue> mq, const MqConfig& cfg);
    ~SlowTaskDispatcher();

    void start();
    void stop();

    // 异步执行慢工具：发布请求 + 等结果
    ToolCallResult executeSlow(const std::string& tool_name, const Json::Value& args);

private:
    void resultLoop();

    std::shared_ptr<IMessageQueue> mq_;
    MqConfig cfg_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<std::promise<std::string>>> pending_;
    std::atomic<uint64_t> next_corr_{1};
};

// 慢任务 Worker：消费 slow-tasks → 执行工具 → 发布结果
class SlowTaskWorker {
public:
    SlowTaskWorker(std::shared_ptr<IMessageQueue> mq,
                   std::shared_ptr<ToolExecutor> executor,
                   const MqConfig& cfg);
    ~SlowTaskWorker();

    void start();
    void stop();

private:
    void loop();

    std::shared_ptr<IMessageQueue> mq_;
    std::shared_ptr<ToolExecutor> executor_;
    MqConfig cfg_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace diag
