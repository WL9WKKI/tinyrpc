#pragma once

#include <memory>
#include <string>

#include <json/json.h>

#include "model/types.h"

namespace diag {

class Planner;
class ToolExecutor;
class SessionManager;

// 诊断结果
struct AgentResult {
    TaskStatus status{TaskStatus::FAILED};
    std::string answer;  // SUCCEEDED 时的结论
    std::string error;   // 失败原因
};

// 极简 ReAct 主循环：Planner → ToolExecutor → SessionManager → 状态机
class ReActAgent {
public:
    ReActAgent(std::shared_ptr<Planner> planner,
               std::shared_ptr<ToolExecutor> executor,
               std::shared_ptr<SessionManager> session);

    AgentResult run(const DiagnosisTask& task);

private:
    std::shared_ptr<Planner> planner_;
    std::shared_ptr<ToolExecutor> executor_;
    std::shared_ptr<SessionManager> session_;
};

// 工厂：从 config.json 组装（session + executor + planner）
std::shared_ptr<ReActAgent> createAgent(const Json::Value& root);

}  // namespace diag
