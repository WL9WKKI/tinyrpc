#include <fstream>
#include <iostream>

#include <json/json.h>

#include "agent/agent.h"
#include "model/types.h"

namespace {

// 直接读 config.json（mock 模式即可跑通闭环，无需 Drogon 事件循环）
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
    // 最小闭环演示：mock LLM + mock 工具 + mock 会话，串起完整 ReAct 流程
    Json::Value config = loadConfigFile("config.json");
    auto agent = diag::createAgent(config);

    diag::DiagnosisTask task;
    task.task_id = 1;
    task.service_name = "order-service";
    task.symptom = "服务内存持续上涨，最终被 OOM 杀死";
    task.max_steps = 10;

    auto result = agent->run(task);

    std::cout << "status: " << diag::taskStatusToString(result.status) << "\n";
    if (result.status == diag::TaskStatus::SUCCEEDED) {
        std::cout << "answer: " << result.answer << "\n";
    } else {
        std::cout << "error: " << result.error << "\n";
    }
    return 0;
}
