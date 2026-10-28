#include "agent/slow_task.h"

#include <chrono>
#include <iostream>
#include <memory>

#include <json/json.h>

namespace diag {

namespace {
std::string stringifyJson(const Json::Value& v) {
    return Json::writeString(Json::StreamWriterBuilder(), v);
}
bool parseJson(const std::string& s, Json::Value& out) {
    Json::CharReaderBuilder b;
    const std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}
ToolCallStatus statusFromStr(const std::string& s) {
    if (s == "SUCCESS") return ToolCallStatus::SUCCESS;
    if (s == "UNKNOWN_TOOL") return ToolCallStatus::UNKNOWN_TOOL;
    if (s == "INVALID_ARGS") return ToolCallStatus::INVALID_ARGS;
    if (s == "TIMEOUT") return ToolCallStatus::TIMEOUT;
    return ToolCallStatus::TOOL_ERROR;
}
}  // namespace

// ---- SlowTaskDispatcher ----

SlowTaskDispatcher::SlowTaskDispatcher(std::shared_ptr<IMessageQueue> mq, const MqConfig& cfg)
    : mq_(std::move(mq)), cfg_(cfg) {}

SlowTaskDispatcher::~SlowTaskDispatcher() { stop(); }

void SlowTaskDispatcher::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&SlowTaskDispatcher::resultLoop, this);
}

void SlowTaskDispatcher::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

ToolCallResult SlowTaskDispatcher::executeSlow(const std::string& tool_name,
                                               const Json::Value& args) {
    uint64_t corr = next_corr_.fetch_add(1);
    std::string corr_str = std::to_string(corr);
    auto promise = std::make_shared<std::promise<std::string>>();
    std::future<std::string> fut = promise->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_[corr] = promise;
    }

    // 发布请求
    Json::Value body;
    body["tool"] = tool_name;
    body["args"] = args;
    MqMessage msg;
    msg.body = stringifyJson(body);
    msg.correlation_id = corr_str;
    msg.reply_to = cfg_.slow_results_queue;
    if (!mq_->publish(cfg_.slow_tasks_queue, msg)) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.erase(corr);
        ToolCallResult fail;
        fail.status = ToolCallStatus::TOOL_ERROR;
        fail.error = "MQ 不可用，慢任务发布失败";
        return fail;
    }

    // 等结果
    ToolCallResult result;
    if (fut.wait_for(std::chrono::milliseconds(cfg_.timeout_ms)) == std::future_status::timeout) {
        result.status = ToolCallStatus::TIMEOUT;
        result.error = "slow task timed out after " + std::to_string(cfg_.timeout_ms) + "ms";
    } else {
        Json::Value r;
        if (parseJson(fut.get(), r)) {
            result.status = statusFromStr(r.isMember("status") ? r["status"].asString() : "TOOL_ERROR");
            result.data = r.isMember("data") ? r["data"] : Json::Value();
            result.error = r.isMember("error") ? r["error"].asString() : "";
        } else {
            result.status = ToolCallStatus::TOOL_ERROR;
            result.error = "invalid result from worker";
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.erase(corr);
    }
    return result;
}

void SlowTaskDispatcher::resultLoop() {
    while (running_) {
        MqMessage msg;
        if (!mq_->consume(cfg_.slow_results_queue, msg, 1000)) {
            continue;
        }
        Json::Value r;
        if (parseJson(msg.body, r)) {
            uint64_t corr = std::stoull(msg.correlation_id);
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_.find(corr);
            if (it != pending_.end()) {
                it->second->set_value(msg.body);
            }
        }
        mq_->ack(cfg_.slow_results_queue, msg);
    }
}

// ---- SlowTaskWorker ----

SlowTaskWorker::SlowTaskWorker(std::shared_ptr<IMessageQueue> mq,
                               std::shared_ptr<ToolExecutor> executor,
                               const MqConfig& cfg)
    : mq_(std::move(mq)), executor_(std::move(executor)), cfg_(cfg) {}

SlowTaskWorker::~SlowTaskWorker() { stop(); }

void SlowTaskWorker::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&SlowTaskWorker::loop, this);
}

void SlowTaskWorker::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void SlowTaskWorker::loop() {
    while (running_) {
        try {
            MqMessage msg;
            if (!mq_->consume(cfg_.slow_tasks_queue, msg, 1000)) {
                continue;
            }
            Json::Value req;
            if (parseJson(msg.body, req) && req.isMember("tool")) {
                std::string tool = req["tool"].asString();
                Json::Value args = req.isMember("args") ? req["args"] : Json::Value(Json::objectValue);
                // 原始同步执行（不再走慢路由，避免死循环）
                ToolCallResult r = executor_->executeRaw(tool, args);
                Json::Value resp;
                resp["status"] = toolCallStatusToString(r.status);
                resp["data"] = r.data;
                resp["error"] = r.error;
                MqMessage out;
                out.body = stringifyJson(resp);
                out.correlation_id = msg.correlation_id;
                mq_->publish(msg.reply_to.empty() ? cfg_.slow_results_queue : msg.reply_to, out);
            }
            mq_->ack(cfg_.slow_tasks_queue, msg);
        } catch (const std::exception& e) {
            std::cerr << "[worker] 处理慢任务异常: " << e.what() << "\n";
        }
    }
}

}  // namespace diag
