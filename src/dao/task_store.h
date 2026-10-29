#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "model/types.h"

namespace diag {

// 任务持久化接口（mock / MySQL 可切换）
class ITaskStore {
public:
    virtual ~ITaskStore() = default;
    virtual uint64_t createTask(const DiagnosisTask& task) = 0;  // 插一行，返回 id
    virtual bool getTask(uint64_t id, DiagnosisTask& out) = 0;
    // 回写 status/current_step/result/error_message（时间戳暂不落库）
    // CAS：仅当任务仍在途（PENDING/RUNNING）才允许回写，终态不可覆盖；返回是否写成功
    virtual bool updateTask(const DiagnosisTask& task) = 0;
    virtual void insertTrace(const ReactStep& step) = 0;
    virtual std::vector<ReactStep> getTrace(uint64_t task_id) = 0;
    // 条件取消：仅当任务非终止态（PENDING/RUNNING）才置 CANCELLED，返回是否成功
    virtual bool cancelTask(uint64_t id) = 0;
    // 删除任务（react_trace 随外键级联删除）
    virtual bool deleteTask(uint64_t id) = 0;
    // 读出在途任务（PENDING/RUNNING），供启动恢复
    virtual std::vector<DiagnosisTask> getInFlightTasks() = 0;
    // 列出任务（新的在前）
    virtual std::vector<DiagnosisTask> listTasks(int limit = 100) = 0;
};

// 内存实现（storage.backend=mock）
class MockTaskStore : public ITaskStore {
public:
    uint64_t createTask(const DiagnosisTask& task) override;
    bool getTask(uint64_t id, DiagnosisTask& out) override;
    bool updateTask(const DiagnosisTask& task) override;
    void insertTrace(const ReactStep& step) override;
    std::vector<ReactStep> getTrace(uint64_t task_id) override;
    bool cancelTask(uint64_t id) override;
    bool deleteTask(uint64_t id) override;
    std::vector<DiagnosisTask> getInFlightTasks() override;
    std::vector<DiagnosisTask> listTasks(int limit = 100) override;

private:
    std::mutex mutex_;
    std::unordered_map<uint64_t, DiagnosisTask> tasks_;
    std::unordered_map<uint64_t, std::vector<ReactStep>> traces_;
};

// MySQL 实现（storage.backend=mysql）
class MySqlTaskStore : public ITaskStore {
public:
    MySqlTaskStore();  // DbClient 延迟获取（getDbClient 须在 drogon run() 之后）
    uint64_t createTask(const DiagnosisTask& task) override;
    bool getTask(uint64_t id, DiagnosisTask& out) override;
    bool updateTask(const DiagnosisTask& task) override;
    void insertTrace(const ReactStep& step) override;
    std::vector<ReactStep> getTrace(uint64_t task_id) override;
    bool cancelTask(uint64_t id) override;
    bool deleteTask(uint64_t id) override;
    std::vector<DiagnosisTask> getInFlightTasks() override;
    std::vector<DiagnosisTask> listTasks(int limit = 100) override;

private:
    drogon::orm::DbClientPtr client_;
    std::mutex client_mutex_;
    drogon::orm::DbClientPtr getClient();  // 延迟拿 DbClient（线程安全）
};

// 生成任务 id（时间戳 + 序列，避免依赖 MySQL LAST_INSERT_ID 的连接池问题）
uint64_t generateTaskId();

// 工厂：从 config 构建（读 storage.backend）
std::shared_ptr<ITaskStore> createTaskStore(const Json::Value& root);

}  // namespace diag
