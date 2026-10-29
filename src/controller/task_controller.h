#pragma once

#include <functional>
#include <memory>

#include <drogon/HttpController.h>

namespace diag {

class ReActAgent;
class ITaskStore;
class CancelManager;
class TaskDispatcher;

// 诊断任务 HTTP 接口
class TaskController : public drogon::HttpController<TaskController, false> {
public:
    TaskController(std::shared_ptr<ReActAgent> agent,
                   std::shared_ptr<ITaskStore> store,
                   std::shared_ptr<CancelManager> canceller,
                   std::shared_ptr<TaskDispatcher> dispatcher,
                   int default_timeout_seconds);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TaskController::submitTask, "/api/tasks", drogon::Post);
    ADD_METHOD_TO(TaskController::listTasks, "/api/tasks", drogon::Get);
    ADD_METHOD_TO(TaskController::getTask, "/api/tasks/{id}", drogon::Get);
    ADD_METHOD_TO(TaskController::getTrace, "/api/tasks/{id}/trace", drogon::Get);
    ADD_METHOD_TO(TaskController::cancelTask, "/api/tasks/{id}/cancel", drogon::Post);
    METHOD_LIST_END

    void submitTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void listTasks(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void getTask(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, uint64_t id);
    void getTrace(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback, uint64_t id);
    void cancelTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, uint64_t id);

private:
    std::shared_ptr<ReActAgent> agent_;
    std::shared_ptr<ITaskStore> store_;
    std::shared_ptr<CancelManager> canceller_;
    std::shared_ptr<TaskDispatcher> dispatcher_;
    int default_timeout_seconds_{300};
};

}  // namespace diag
