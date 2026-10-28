#include <fstream>
#include <iostream>
#include <memory>

#include <drogon/drogon.h>
#include <json/json.h>

#include "agent/agent.h"
#include "agent/cancel_manager.h"
#include "agent/slow_task.h"
#include "controller/task_controller.h"
#include "dao/task_store.h"
#include "executor/tool_executor.h"
#include "mq/mq_client.h"
#include "planner/planner.h"
#include "session/session_manager.h"

namespace {

// 业务配置单独读一份（agent/session/redis/tools/executor/storage）
Json::Value loadConfigFile(const std::string& path) {
    Json::Value root;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "无法打开 " << path << "\n";
        return root;
    }
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, ifs, &root, &errs)) {
        std::cerr << "config.json 解析失败: " << errs << "\n";
    }
    return root;
}

}  // namespace

int main() {
    // Drogon 自身配置（listeners / db_clients）
    drogon::app().loadConfigFile("config.json");
    // 业务配置（给各模块工厂用）
    Json::Value config = loadConfigFile("config.json");

    // 组装依赖
    auto session = diag::createSessionManager(config);
    auto executor = diag::createToolExecutor(config);
    // 慢任务：MQ + 异步 worker + 分发器
    auto mq = diag::createMessageQueue(config);
    auto mqCfg = diag::parseMqConfig(config);
    auto slowDispatcher = std::make_shared<diag::SlowTaskDispatcher>(mq, mqCfg);
    auto slowWorker = std::make_shared<diag::SlowTaskWorker>(mq, executor, mqCfg);
    slowDispatcher->start();
    slowWorker->start();
    executor->setSlowDispatcher(slowDispatcher);
    auto planner = diag::createPlanner(config, executor->describeTools());
    auto store = diag::createTaskStore(config);
    auto canceller = std::make_shared<diag::CancelManager>();
    auto agent = std::make_shared<diag::ReActAgent>(planner, executor, session, store, canceller);

    // 注册 HTTP 接口
    auto controller = std::make_shared<diag::TaskController>(agent, store, canceller);
    drogon::app().registerController(controller);

    drogon::app().run();
    return 0;
}
