#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <json/json.h>

#include "model/types.h"

namespace diag {

class ReActAgent;
class ITaskStore;

// 并发参数（来自 config.json 的 concurrency 段）
struct ConcurrencyConfig {
    int max_workers = 4;          // 工作线程数 = 并发上限
    int queue_size = 100;         // 等待队列容量；<0 表示无界
    bool per_service_lock = true; // 同 service 串行（防重复诊断）
    int queue_timeout_ms = 0;     // 满时最多等待；0 = 立即拒绝
    int task_timeout_seconds = 300;  // 任务超时截止时长（默认 5 分钟）
};

// 提交决策
struct SubmitDecision {
    bool accepted = false;
    enum Reason { OK, QUEUE_FULL, SERVICE_BUSY, STOPPING } reason = OK;
};

// 任务调度器：固定线程池 + 有界队列 + 同服务互斥 + 优雅停机
class TaskDispatcher {
public:
    explicit TaskDispatcher(const ConcurrencyConfig& cfg);
    ~TaskDispatcher();

    void setAgent(std::shared_ptr<ReActAgent> agent);
    void setStore(std::shared_ptr<ITaskStore> store);

    // 两阶段提交：先原子占坑，建表成功后再 enqueue；失败用 rollback 释放
    SubmitDecision tryReserve(const std::string& service);
    bool enqueue(const DiagnosisTask& task);  // false = 已停服，需调用方 rollback
    void rollback(const std::string& service);

    void start();
    void stop();
    void recover();  // 启动恢复：PENDING 重跑，RUNNING 删除后重建为 PENDING 再跑

private:
    void workerLoop();

    ConcurrencyConfig cfg_;
    std::shared_ptr<ReActAgent> agent_;
    std::shared_ptr<ITaskStore> store_;
    std::vector<std::thread> workers_;
    std::deque<DiagnosisTask> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_{false};
    std::unordered_map<std::string, int> running_;  // service -> 占坑任务数
};

// 从 config.json 解析 concurrency 段
ConcurrencyConfig parseConcurrencyConfig(const Json::Value& root);

}  // namespace diag
