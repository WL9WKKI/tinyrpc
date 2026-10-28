#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <json/json.h>

#include "planner/llm_client.h"
#include "session/session_types.h"

namespace diag {

// Planner 的决策动作
enum class PlannerAction : uint8_t {
    CALL_TOOL = 0,  // 需要调用工具
    FINISH    = 1,  // 给出最终结论
    ERROR     = 2,  // LLM 调用失败 / 重试耗尽 / 解析失败
};

// 一次工具调用
struct ToolCall {
    std::string tool;
    Json::Value args;
};

// Planner 输出（一次决策）
struct PlanResult {
    PlannerAction action{PlannerAction::ERROR};
    ToolCall call;        // action == CALL_TOOL 时有效
    std::string answer;   // action == FINISH 时有效
    std::string error;    // action == ERROR 时有效
};

// 推理规划器：调 LLM、提取工具调用 JSON、容错重试
class Planner {
public:
    Planner(std::shared_ptr<ILLMClient> client, const Json::Value& tools, int max_retries);

    // 给定对话，决策下一步
    PlanResult plan(const std::vector<ChatMessage>& messages);

    // 生成 system 提示词（含工具清单）
    std::string buildSystemPrompt() const;

private:
    std::shared_ptr<ILLMClient> client_;
    Json::Value tools_;
    int max_retries_;
    std::string system_prompt_;
};

const char* plannerActionToString(PlannerAction a);

// 工厂：从 config 构建（读 agent.llm.* + agent.max_retries）
std::shared_ptr<Planner> createPlanner(const Json::Value& root, const Json::Value& tools);

}  // namespace diag
