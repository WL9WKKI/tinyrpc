#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // 启用 timegm / gmtime_r（Linux）
#endif

#include "dao/task_store.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

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

namespace {

// time_point -> "YYYY-MM-DD HH:MM:SS.mmm"（UTC）；0 时刻返回空串
std::string deadlineToDb(const std::chrono::system_clock::time_point& tp) {
    if (tp.time_since_epoch().count() == 0) return "";
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    std::time_t sec = static_cast<std::time_t>(ms / 1000);
    int milli = static_cast<int>(ms % 1000);
    struct tm t{};
    gmtime_r(&sec, &t);
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    std::ostringstream os;
    os << buf << '.' << std::setw(3) << std::setfill('0') << milli;
    return os.str();
}

// "YYYY-MM-DD HH:MM:SS[.mmm]"（UTC）-> time_point；空串/非法返回 0 时刻
std::chrono::system_clock::time_point dbToDeadline(const std::string& s) {
    if (s.empty()) return {};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0, ms = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d.%d", &y, &mo, &d, &h, &mi, &sec, &ms) < 6 &&
        std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 6) {
        return {};
    }
    struct tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = sec;
    t.tm_isdst = 0;  // UTC 无夏令时
    return std::chrono::system_clock::time_point(
        std::chrono::seconds(timegm(&t)) + std::chrono::milliseconds(ms));
}

}  // namespace

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

bool MockTaskStore::updateTask(const DiagnosisTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task.task_id);
    if (it == tasks_.end()) return false;
    // CAS：终态不可覆盖，防止迟到写覆盖 CANCELLED/SUCCEEDED 等
    if (TaskStateMachine::isTerminal(it->second.status)) {
        std::cerr << "[mock] updateTask CAS 失败 task " << task.task_id << "\n";
        return false;
    }
    it->second.status = task.status;
    it->second.current_step = task.current_step;
    it->second.result = task.result;
    it->second.error_message = task.error_message;
    it->second.confidence = task.confidence;
    it->second.started_at = task.started_at;
    it->second.finished_at = task.finished_at;
    it->second.updated_at = std::chrono::system_clock::now();
    return true;
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

bool MockTaskStore::deleteTask(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = tasks_.erase(id);
    traces_.erase(id);
    return t > 0;
}

std::vector<DiagnosisTask> MockTaskStore::getInFlightTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DiagnosisTask> out;
    for (const auto& kv : tasks_) {
        if (!TaskStateMachine::isTerminal(kv.second.status)) out.push_back(kv.second);
    }
    return out;
}

std::vector<DiagnosisTask> MockTaskStore::listTasks(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DiagnosisTask> out;
    out.reserve(tasks_.size());
    for (const auto& kv : tasks_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(),
              [](const DiagnosisTask& a, const DiagnosisTask& b) { return a.task_id > b.task_id; });
    if (static_cast<int>(out.size()) > limit) out.resize(limit);
    return out;
}

// ---- MySqlTaskStore ----

MySqlTaskStore::MySqlTaskStore() : client_(nullptr) {}

drogon::orm::DbClientPtr MySqlTaskStore::getClient() {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (!client_) {
        // getDbClient 必须在 drogon run() 之后调用，这里延迟到首次使用
        client_ = drogon::app().getDbClient("default");
    }
    return client_;
}

uint64_t MySqlTaskStore::createTask(const DiagnosisTask& task) {
    uint64_t id = task.task_id ? task.task_id : generateTaskId();
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return id;
    }
    try {
        client->execSqlSync(
            "INSERT INTO diagnosis_task (id, service_name, symptom, severity, status, max_steps, current_step, deadline) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            id, task.service_name, task.symptom,
            static_cast<int>(task.severity), static_cast<int>(task.status),
            task.max_steps, task.current_step, deadlineToDb(task.deadline));
    } catch (const std::exception& e) {
        std::cerr << "[mysql] createTask 失败: " << e.what() << "\n";
    }
    return id;
}

bool MySqlTaskStore::getTask(uint64_t id, DiagnosisTask& out) {
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return false;
    }
    try {
        auto r = client->execSqlSync("SELECT * FROM diagnosis_task WHERE id=?", id);
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
        if (!row["deadline"].isNull()) out.deadline = dbToDeadline(row["deadline"].as<std::string>());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[mysql] getTask 失败: " << e.what() << "\n";
        return false;
    }
}

bool MySqlTaskStore::updateTask(const DiagnosisTask& task) {
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return false;
    }
    try {
        // CAS：仅当任务仍在途（PENDING/RUNNING）才回写，终态不可覆盖
        auto r = client->execSqlSync(
            "UPDATE diagnosis_task SET status=?, current_step=?, result=?, error_message=?, confidence=? "
            "WHERE id=? AND status IN (?,?)",
            static_cast<int>(task.status), task.current_step, task.result, task.error_message,
            task.confidence, task.task_id,
            static_cast<int>(TaskStatus::PENDING), static_cast<int>(TaskStatus::RUNNING));
        if (r.affectedRows() == 0) {
            std::cerr << "[mysql] updateTask CAS 失败 task " << task.task_id << "（已被改为终态）\n";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[mysql] updateTask 失败: " << e.what() << "\n";
        return false;
    }
}

void MySqlTaskStore::insertTrace(const ReactStep& step) {
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return;
    }
    try {
        client->execSqlSync(
            "INSERT INTO react_trace (task_id, step_no, thought, action, action_input, observation) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            step.task_id, step.step_no, step.thought, step.action, step.action_input, step.observation);
    } catch (const std::exception& e) {
        std::cerr << "[mysql] insertTrace 失败: " << e.what() << "\n";
    }
}

std::vector<ReactStep> MySqlTaskStore::getTrace(uint64_t task_id) {
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return {};
    }
    try {
        auto r = client->execSqlSync(
            "SELECT * FROM react_trace WHERE task_id=? ORDER BY step_no", task_id);
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
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return false;
    }
    try {
        // 条件更新（CAS）：仅当 status 为 PENDING/RUNNING 才取消
        client->execSqlSync(
            "UPDATE diagnosis_task SET status=? WHERE id=? AND status IN (?,?)",
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

bool MySqlTaskStore::deleteTask(uint64_t id) {
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return false;
    }
    try {
        auto r = client->execSqlSync("DELETE FROM diagnosis_task WHERE id=?", id);
        return r.affectedRows() > 0;
    } catch (const std::exception& e) {
        std::cerr << "[mysql] deleteTask 失败: " << e.what() << "\n";
        return false;
    }
}

std::vector<DiagnosisTask> MySqlTaskStore::getInFlightTasks() {
    std::vector<DiagnosisTask> out;
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return out;
    }
    try {
        auto r = client->execSqlSync(
            "SELECT id FROM diagnosis_task WHERE status IN (?,?)",
            static_cast<int>(TaskStatus::PENDING), static_cast<int>(TaskStatus::RUNNING));
        for (const auto& row : r) {
            DiagnosisTask t;
            if (getTask(row["id"].as<uint64_t>(), t)) out.push_back(std::move(t));
        }
    } catch (const std::exception& e) {
        std::cerr << "[mysql] getInFlightTasks 失败: " << e.what() << "\n";
    }
    return out;
}

std::vector<DiagnosisTask> MySqlTaskStore::listTasks(int limit) {
    std::vector<DiagnosisTask> out;
    auto client = getClient();
    if (!client) {
        std::cerr << "[mysql] DbClient 未就绪\n";
        return out;
    }
    try {
        auto r = client->execSqlSync(
            "SELECT id FROM diagnosis_task ORDER BY created_at DESC LIMIT ?", limit);
        for (const auto& row : r) {
            DiagnosisTask t;
            if (getTask(row["id"].as<uint64_t>(), t)) out.push_back(std::move(t));
        }
    } catch (const std::exception& e) {
        std::cerr << "[mysql] listTasks 失败: " << e.what() << "\n";
    }
    return out;
}

// ---- 工厂 ----

std::shared_ptr<ITaskStore> createTaskStore(const Json::Value& root) {
    std::string backend = (root.isMember("storage") && root["storage"].isMember("backend"))
                              ? root["storage"]["backend"].asString() : "mock";
    if (backend == "mysql") {
        // DbClient 在 MySqlTaskStore 内部延迟获取（getDbClient 须在 run() 之后）
        return std::make_shared<MySqlTaskStore>();
    }
    return std::make_shared<MockTaskStore>();
}

}  // namespace diag
