#include "dao/task_store.h"

#include <atomic>
#include <chrono>
#include <iostream>

#include <drogon/drogon.h>

#include "state/state_machine.h"

namespace diag {

uint64_t generateTaskId() {
    static std::atomic<uint64_t> seq{0};
    uint64_t ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    return (ms << 20) | (seq.fetch_add(1) & 0xFFFFF);
}

// ---- MockTaskStore ----

uint64_t MockTaskStore::createTask(const DiagnosisTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t id = task.task_id ? task.task_id : generateTaskId();
    DiagnosisTask t = task;
    t.task_id = id;
    if (t.created_at.time_since_epoch().count() == 0) {
        t.created_at = std::chrono::system_clock::now();
        t.updated_at = t.created_at;
    }
    tasks_[id] = t;
    traces_[id] = {};
    return id;
}

bool MockTaskStore::getTask(uint64_t id, DiagnosisTask& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;
    out = it->second;
    return true;
}

void MockTaskStore::updateTask(const DiagnosisTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task.task_id);
    if (it == tasks_.end()) return;
    it->second.status = task.status;
    it->second.current_step = task.current_step;
    it->second.result = task.result;
    it->second.error_message = task.error_message;
    it->second.confidence = task.confidence;
    it->second.started_at = task.started_at;
    it->second.finished_at = task.finished_at;
    it->second.updated_at = std::chrono::system_clock::now();
}

void MockTaskStore::insertTrace(const ReactStep& step) {
    std::lock_guard<std::mutex> lock(mutex_);
    traces_[step.task_id].push_back(step);
}

std::vector<ReactStep> MockTaskStore::getTrace(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = traces_.find(task_id);
    if (it == traces_.end()) return {};
    return it->second;
}

bool MockTaskStore::cancelTask(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end() || TaskStateMachine::isTerminal(it->second.status)) return false;
    it->second.status = TaskStatus::CANCELLED;
    it->second.finished_at = std::chrono::system_clock::now();
    it->second.updated_at = std::chrono::system_clock::now();
    return true;
}

// ---- MySqlTaskStore ----

MySqlTaskStore::MySqlTaskStore(drogon::orm::DbClientPtr client) : client_(std::move(client)) {}

uint64_t MySqlTaskStore::createTask(const DiagnosisTask& task) {
    uint64_t id = task.task_id ? task.task_id : generateTaskId();
    try {
        client_->execSqlSync(
            "INSERT INTO diagnosis_task (id, service_name, symptom, severity, status, max_steps, current_step) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7)",
            id, task.service_name, task.symptom,
            static_cast<int>(task.severity), static_cast<int>(task.status),
            task.max_steps, task.current_step);
    } catch (const std::exception& e) {
        std::cerr << "[mysql] createTask 失败: " << e.what() << "\n";
    }
    return id;
}

bool MySqlTaskStore::getTask(uint64_t id, DiagnosisTask& out) {
    try {
        auto r = client_->execSqlSync("SELECT * FROM diagnosis_task WHERE id=$1", id);
        if (r.empty()) return false;
        const auto& row = r[0];
        out.task_id = row["id"].as<uint64_t>();
        out.service_name = row["service_name"].as<std::string>();
        out.symptom = row["symptom"].as<std::string>();
        out.severity = severityFromInt(static_cast<uint8_t>(row["severity"].as<int>()));
        out.status = taskStatusFromInt(static_cast<uint8_t>(row["status"].as<int>()));
        out.max_steps = row["max_steps"].as<int>();
        out.current_step = row["current_step"].as<int>();
        if (!row["result"].isNull()) out.result = row["result"].as<std::string>();
        if (!row["error_message"].isNull()) out.error_message = row["error_message"].as<std::string>();
        if (!row["confidence"].isNull()) out.confidence = row["confidence"].as<double>();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[mysql] getTask 失败: " << e.what() << "\n";
        return false;
    }
}

void MySqlTaskStore::updateTask(const DiagnosisTask& task) {
    try {
        client_->execSqlSync(
            "UPDATE diagnosis_task SET status=$1, current_step=$2, result=$3, error_message=$4, confidence=$5 WHERE id=$6",
            static_cast<int>(task.status), task.current_step, task.result, task.error_message,
            task.confidence, task.task_id);
    } catch (const std::exception& e) {
        std::cerr << "[mysql] updateTask 失败: " << e.what() << "\n";
    }
}

void MySqlTaskStore::insertTrace(const ReactStep& step) {
    try {
        client_->execSqlSync(
            "INSERT INTO react_trace (task_id, step_no, thought, action, action_input, observation) "
            "VALUES ($1, $2, $3, $4, $5, $6)",
            step.task_id, step.step_no, step.thought, step.action, step.action_input, step.observation);
    } catch (const std::exception& e) {
        std::cerr << "[mysql] insertTrace 失败: " << e.what() << "\n";
    }
}

std::vector<ReactStep> MySqlTaskStore::getTrace(uint64_t task_id) {
    try {
        auto r = client_->execSqlSync(
            "SELECT * FROM react_trace WHERE task_id=$1 ORDER BY step_no", task_id);
        std::vector<ReactStep> out;
        out.reserve(r.size());
        for (const auto& row : r) {
            ReactStep s;
            s.task_id = row["task_id"].as<uint64_t>();
            s.step_no = row["step_no"].as<int>();
            if (!row["thought"].isNull()) s.thought = row["thought"].as<std::string>();
            if (!row["action"].isNull()) s.action = row["action"].as<std::string>();
            if (!row["action_input"].isNull()) s.action_input = row["action_input"].as<std::string>();
            if (!row["observation"].isNull()) s.observation = row["observation"].as<std::string>();
            out.push_back(std::move(s));
        }
        return out;
    } catch (const std::exception& e) {
        std::cerr << "[mysql] getTrace 失败: " << e.what() << "\n";
        return {};
    }
}

bool MySqlTaskStore::cancelTask(uint64_t id) {
    try {
        // 条件更新（CAS）：仅当 status 为 PENDING/RUNNING 才取消
        client_->execSqlSync(
            "UPDATE diagnosis_task SET status=$1 WHERE id=$2 AND status IN ($3,$4)",
            static_cast<int>(TaskStatus::CANCELLED), id,
            static_cast<int>(TaskStatus::PENDING), static_cast<int>(TaskStatus::RUNNING));
        DiagnosisTask t;
        if (!getTask(id, t)) return false;
        return t.status == TaskStatus::CANCELLED;
    } catch (const std::exception& e) {
        std::cerr << "[mysql] cancelTask 失败: " << e.what() << "\n";
        return false;
    }
}

// ---- 工厂 ----

std::shared_ptr<ITaskStore> createTaskStore(const Json::Value& root) {
    std::string backend = (root.isMember("storage") && root["storage"].isMember("backend"))
                              ? root["storage"]["backend"].asString() : "mock";
    if (backend == "mysql") {
        auto client = drogon::app().getDbClient("default");
        return std::make_shared<MySqlTaskStore>(client);
    }
    return std::make_shared<MockTaskStore>();
}

}  // namespace diag
