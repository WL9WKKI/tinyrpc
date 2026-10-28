#include "controller/task_controller.h"

#include <chrono>
#include <thread>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "agent/agent.h"
#include "agent/cancel_manager.h"
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
}  // namespace

TaskController::TaskController(std::shared_ptr<ReActAgent> agent,
                               std::shared_ptr<ITaskStore> store,
                               std::shared_ptr<CancelManager> canceller)
    : agent_(std::move(agent)), store_(std::move(store)), canceller_(std::move(canceller)) {}

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
    task.status = TaskStatus::PENDING;
    task.created_at = std::chrono::system_clock::now();
    task.updated_at = task.created_at;

    store_->createTask(task);

    // 后台线程跑诊断循环（不阻塞 HTTP）
    std::thread([agent = agent_, task]() { agent->run(task); }).detach();

    Json::Value resp;
    resp["task_id"] = static_cast<Json::UInt64>(task.task_id);
    resp["status"] = "PENDING";
    callback(jsonResponse(resp, drogon::k201Created));
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
    v["result"] = task.result;
    v["error_message"] = task.error_message;
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
    if (TaskStateMachine::isTerminal(task.status)) {
        callback(jsonResponse(errorJson("task already finished"), drogon::k409Conflict));
        return;
    }
    task.status = TaskStatus::CANCELLED;
    store_->updateTask(task);
    canceller_->cancel(id);

    Json::Value v;
    v["task_id"] = static_cast<Json::UInt64>(id);
    v["status"] = "CANCELLED";
    callback(jsonResponse(v));
}

}  // namespace diag
