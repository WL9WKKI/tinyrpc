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
    virtual void updateTask(const DiagnosisTask& task) = 0;
    virtual void insertTrace(const ReactStep& step) = 0;
    virtual std::vector<ReactStep> getTrace(uint64_t task_id) = 0;
    // 条件取消：仅当任务非终止态（PENDING/RUNNING）才置 CANCELLED，返回是否成功
    virtual bool cancelTask(uint64_t id) = 0;
};

// 内存实现（storage.backend=mock）
class MockTaskStore : public ITaskStore {
public:
    uint64_t createTask(const DiagnosisTask& task) override;
    bool getTask(uint64_t id, DiagnosisTask& out) override;
    void updateTask(const DiagnosisTask& task) override;
    void insertTrace(const ReactStep& step) override;
    std::vector<ReactStep> getTrace(uint64_t task_id) override;
    bool cancelTask(uint64_t id) override;

private:
    std::mutex mutex_;
    std::unordered_map<uint64_t, DiagnosisTask> tasks_;
    std::unordered_map<uint64_t, std::vector<ReactStep>> traces_;
};

// MySQL 实现（storage.backend=mysql）
class MySqlTaskStore : public ITaskStore {
public:
    explicit MySqlTaskStore(drogon::orm::DbClientPtr client);
    uint64_t createTask(const DiagnosisTask& task) override;
    bool getTask(uint64_t id, DiagnosisTask& out) override;
    void updateTask(const DiagnosisTask& task) override;
    void insertTrace(const ReactStep& step) override;
    std::vector<ReactStep> getTrace(uint64_t task_id) override;
    bool cancelTask(uint64_t id) override;

private:
    drogon::orm::DbClientPtr client_;
};

// 生成任务 id（时间戳 + 序列，避免依赖 MySQL LAST_INSERT_ID 的连接池问题）
uint64_t generateTaskId();

// 工厂：从 config 构建（读 storage.backend）
std::shared_ptr<ITaskStore> createTaskStore(const Json::Value& root);

}  // namespace diag
