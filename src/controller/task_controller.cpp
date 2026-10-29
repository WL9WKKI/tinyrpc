#include "controller/task_controller.h"

#include <chrono>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "agent/agent.h"
#include "agent/cancel_manager.h"
#include "agent/task_dispatcher.h"
#include "dao/task_store.h"
#include "state/state_machine.h"

namespace diag {

namespace {
drogon::HttpResponsePtr jsonResponse(const Json::Value& v,
                                     drogon::HttpStatusCode code = drogon::k200OK) {
    auto resp = drogon::HttpResponse::newHttpJsonResponse(v);
    resp->setStatusCode(code);
    return resp;
}
Json::Value errorJson(const std::string& msg) {
    Json::Value v;
    v["error"] = msg;
    return v;
}
bool parseJson(const std::string& s, Json::Value& out) {
    Json::CharReaderBuilder b;
    const std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}
}  // namespace

TaskController::TaskController(std::shared_ptr<ReActAgent> agent,
                               std::shared_ptr<ITaskStore> store,
                               std::shared_ptr<CancelManager> canceller,
                               std::shared_ptr<TaskDispatcher> dispatcher,
                               int default_timeout_seconds)
    : agent_(std::move(agent)), store_(std::move(store)), canceller_(std::move(canceller)),
      dispatcher_(std::move(dispatcher)), default_timeout_seconds_(default_timeout_seconds) {}

void TaskController::submitTask(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto jsonPtr = req->getJsonObject();
    if (!jsonPtr) {
        callback(jsonResponse(errorJson("invalid JSON body"), drogon::k400BadRequest));
        return;
    }
    const Json::Value& json = *jsonPtr;
    std::string service_name = json.isMember("service_name") ? json["service_name"].asString() : "";
    std::string symptom = json.isMember("symptom") ? json["symptom"].asString() : "";
    if (service_name.empty() || symptom.empty()) {
        callback(jsonResponse(errorJson("service_name and symptom are required"),
                              drogon::k400BadRequest));
        return;
    }

    DiagnosisTask task;
    task.task_id = generateTaskId();
    task.service_name = service_name;
    task.symptom = symptom;
    task.severity = json.isMember("severity")
                        ? severityFromInt(static_cast<uint8_t>(json["severity"].asInt()))
                        : Severity::P2;
    task.max_steps = json.isMember("max_steps") ? json["max_steps"].asInt() : 10;
    // 超时截止：请求可覆盖 timeout_seconds，否则用 config 默认值
    int timeout_seconds = default_timeout_seconds_;
    if (json.isMember("timeout_seconds") && json["timeout_seconds"].isInt()) {
        timeout_seconds = json["timeout_seconds"].asInt();
    }
    if (timeout_seconds > 0) {
        task.deadline = std::chrono::system_clock::now() + std::chrono::seconds(timeout_seconds);
    }
    task.status = TaskStatus::PENDING;
    task.created_at = std::chrono::system_clock::now();
    task.updated_at = task.created_at;

    // 并发控制：先占坑（原子），避免被拒后留下孤儿 PENDING 行
    SubmitDecision d = dispatcher_->tryReserve(service_name);
    if (!d.accepted) {
        if (d.reason == SubmitDecision::SERVICE_BUSY) {
            callback(jsonResponse(errorJson("该服务已有诊断进行中"), drogon::k409Conflict));
        } else if (d.reason == SubmitDecision::QUEUE_FULL) {
            callback(jsonResponse(errorJson("诊断队列已满，请稍后重试"), drogon::k429TooManyRequests));
        } else {
            callback(jsonResponse(errorJson("服务正在停机"), drogon::k503ServiceUnavailable));
        }
        return;
    }

    store_->createTask(task);

    if (!dispatcher_->enqueue(task)) {
        // 已停服：释放占位 + 落 FAILED
        dispatcher_->rollback(service_name);
        task.status = TaskStatus::FAILED;
        task.error_message = "shutting down";
        task.finished_at = std::chrono::system_clock::now();
        store_->updateTask(task);
        callback(jsonResponse(errorJson("服务正在停机"), drogon::k503ServiceUnavailable));
        return;
    }

    Json::Value resp;
    resp["task_id"] = static_cast<Json::UInt64>(task.task_id);
    resp["status"] = "PENDING";
    callback(jsonResponse(resp, drogon::k201Created));
}

void TaskController::listTasks(const drogon::HttpRequestPtr&,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto tasks = store_->listTasks(100);
    Json::Value arr(Json::arrayValue);
    for (const auto& t : tasks) {
        Json::Value e;
        e["task_id"] = static_cast<Json::UInt64>(t.task_id);
        e["service_name"] = t.service_name;
        e["symptom"] = t.symptom;
        e["status"] = taskStatusToString(t.status);
        e["current_step"] = t.current_step;
        e["confidence"] = t.confidence;
        e["created_at"] = static_cast<Json::Int64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                t.created_at.time_since_epoch()).count());
        arr.append(e);
    }
    Json::Value v;
    v["tasks"] = arr;
    callback(jsonResponse(v));
}

void TaskController::getTask(const drogon::HttpRequestPtr&,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             uint64_t id) {
    DiagnosisTask task;
    if (!store_->getTask(id, task)) {
        callback(jsonResponse(errorJson("task not found"), drogon::k404NotFound));
        return;
    }
    Json::Value v;
    v["task_id"] = static_cast<Json::UInt64>(task.task_id);
    v["service_name"] = task.service_name;
    v["symptom"] = task.symptom;
    v["severity"] = severityToString(task.severity);
    v["status"] = taskStatusToString(task.status);
    v["current_step"] = task.current_step;
    v["max_steps"] = task.max_steps;
    v["confidence"] = task.confidence;
    v["error_message"] = task.error_message;
    // result 可能是 JSON {answer,evidence,level}，解析出子字段
    Json::Value r;
    if (parseJson(task.result, r) && r.isMember("answer")) {
        v["answer"] = r["answer"];
        v["evidence"] = r.isMember("evidence") ? r["evidence"] : Json::Value(Json::arrayValue);
        v["level"] = r.isMember("level") ? r["level"] : "";
    } else {
        v["answer"] = task.result;
    }
    callback(jsonResponse(v));
}

void TaskController::getTrace(const drogon::HttpRequestPtr&,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              uint64_t id) {
    auto steps = store_->getTrace(id);
    Json::Value arr(Json::arrayValue);
    for (const auto& s : steps) {
        Json::Value e;
        e["step_no"] = s.step_no;
        e["thought"] = s.thought;
        e["action"] = s.action;
        e["action_input"] = s.action_input;
        e["observation"] = s.observation;
        arr.append(e);
    }
    callback(jsonResponse(arr));
}

void TaskController::cancelTask(const drogon::HttpRequestPtr&,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                uint64_t id) {
    DiagnosisTask task;
    if (!store_->getTask(id, task)) {
        callback(jsonResponse(errorJson("task not found"), drogon::k404NotFound));
        return;
    }
    if (!store_->cancelTask(id)) {
        callback(jsonResponse(errorJson("task already finished"), drogon::k409Conflict));
        return;
    }
    canceller_->cancel(id);

    Json::Value v;
    v["task_id"] = static_cast<Json::UInt64>(id);
    v["status"] = "CANCELLED";
    callback(jsonResponse(v));
}

}  // namespace diag
