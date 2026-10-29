#include "agent/task_dispatcher.h"

#include <chrono>
#include <iostream>

#include "agent/agent.h"
#include "dao/task_store.h"

namespace diag {

ConcurrencyConfig parseConcurrencyConfig(const Json::Value& root) {
    ConcurrencyConfig cfg;
    if (!root.isMember("concurrency") || !root["concurrency"].isObject()) return cfg;
    const Json::Value& c = root["concurrency"];
    if (c.isMember("max_workers") && c["max_workers"].isInt()) cfg.max_workers = c["max_workers"].asInt();
    if (c.isMember("queue_size") && c["queue_size"].isInt()) cfg.queue_size = c["queue_size"].asInt();
    if (c.isMember("per_service_lock") && c["per_service_lock"].isBool()) cfg.per_service_lock = c["per_service_lock"].asBool();
    if (c.isMember("queue_timeout_ms") && c["queue_timeout_ms"].isInt()) cfg.queue_timeout_ms = c["queue_timeout_ms"].asInt();
    if (c.isMember("task_timeout_seconds") && c["task_timeout_seconds"].isInt())
        cfg.task_timeout_seconds = c["task_timeout_seconds"].asInt();
    if (cfg.max_workers <= 0) cfg.max_workers = 1;  // 至少 1 个 worker
    return cfg;
}

TaskDispatcher::TaskDispatcher(const ConcurrencyConfig& cfg) : cfg_(cfg) {}

TaskDispatcher::~TaskDispatcher() { stop(); }

void TaskDispatcher::setAgent(std::shared_ptr<ReActAgent> agent) { agent_ = std::move(agent); }
void TaskDispatcher::setStore(std::shared_ptr<ITaskStore> store) { store_ = std::move(store); }

SubmitDecision TaskDispatcher::tryReserve(const std::string& service) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopping_) return {false, SubmitDecision::STOPPING};
    if (cfg_.per_service_lock && running_.count(service) && running_[service] > 0) {
        return {false, SubmitDecision::SERVICE_BUSY};
    }
    if (cfg_.queue_size >= 0 && static_cast<int>(queue_.size()) >= cfg_.queue_size) {
        if (cfg_.queue_timeout_ms <= 0) return {false, SubmitDecision::QUEUE_FULL};
        // 满则等 worker 腾位（worker 出队后会 notify）
        cv_.wait_for(lock, std::chrono::milliseconds(cfg_.queue_timeout_ms),
                     [&] { return stopping_ || static_cast<int>(queue_.size()) < cfg_.queue_size; });
        if (stopping_) return {false, SubmitDecision::STOPPING};
        if (static_cast<int>(queue_.size()) >= cfg_.queue_size) return {false, SubmitDecision::QUEUE_FULL};
    }
    ++running_[service];
    return {true, SubmitDecision::OK};
}

bool TaskDispatcher::enqueue(const DiagnosisTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return false;
    queue_.push_back(task);
    cv_.notify_all();
    return true;
}

void TaskDispatcher::rollback(const std::string& service) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = running_.find(service);
    if (it != running_.end()) {
        if (--it->second <= 0) running_.erase(it);
    }
}

void TaskDispatcher::start() {
    if (!workers_.empty()) return;  // 已启动
    workers_.reserve(cfg_.max_workers);
    for (int i = 0; i < cfg_.max_workers; ++i) {
        workers_.emplace_back(&TaskDispatcher::workerLoop, this);
    }
}

void TaskDispatcher::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();

    // 停服时未启动的排队任务落为 FAILED（PENDING → FAILED 合法），不留孤儿 PENDING 行
    if (store_) {
        std::vector<DiagnosisTask> leftover;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            leftover.assign(queue_.begin(), queue_.end());
            queue_.clear();
            running_.clear();
        }
        for (auto& t : leftover) {
            t.status = TaskStatus::FAILED;
            t.error_message = "shutting down";
            t.finished_at = std::chrono::system_clock::now();
            store_->updateTask(t);
        }
    }
}

void TaskDispatcher::recover() {
    if (!store_) return;
    auto inflight = store_->getInFlightTasks();
    auto now = std::chrono::system_clock::now();
    for (auto task : inflight) {
        // RUNNING 无法断点续跑且 RUNNING→PENDING 非法：删除（级联清 trace）后重建为 PENDING 重跑
        if (task.status == TaskStatus::RUNNING) {
            store_->deleteTask(task.task_id);
            task.status = TaskStatus::PENDING;
            task.current_step = 0;
            task.result.clear();
            task.error_message.clear();
            task.confidence = 0.0;
            task.started_at.reset();
            task.finished_at.reset();
            task.deadline = now + std::chrono::seconds(cfg_.task_timeout_seconds);
            store_->createTask(task);
        }
        // 占坑 + 入队（启动期队列空、无 running，理论上不会拒绝）
        SubmitDecision d = tryReserve(task.service_name);
        if (!d.accepted) {
            std::cerr << "[dispatcher] recover 拒绝任务 " << task.task_id << "\n";
            continue;
        }
        if (!enqueue(task)) {
            rollback(task.service_name);
        }
    }
}

void TaskDispatcher::workerLoop() {
    while (true) {
        DiagnosisTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) break;  // 停服：不再接新任务，队列里的由 stop() 落 FAILED
            task = std::move(queue_.front());
            queue_.pop_front();
            cv_.notify_all();  // 腾出位置，唤醒满队等待者
        }

        // 锁外执行诊断循环（run 内部逐操作加锁；真实 LLM 客户端无共享状态）
        try {
            if (agent_) agent_->run(task);
        } catch (const std::exception& e) {
            std::cerr << "[dispatcher] task " << task.task_id << " 异常: " << e.what() << "\n";
        }

        // 释放同服务占位
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = running_.find(task.service_name);
            if (it != running_.end()) {
                if (--it->second <= 0) running_.erase(it);
            }
        }
    }
}

}  // namespace diag
