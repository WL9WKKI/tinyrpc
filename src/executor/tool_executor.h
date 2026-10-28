#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <json/json.h>

#include "tools/base_tool.h"

namespace diag {

// 工具调用的执行状态（比工具自身的 ToolStatus 更细，含白名单/校验失败）
enum class ToolCallStatus : uint8_t {
    SUCCESS = 0,       // 执行成功
    UNKNOWN_TOOL = 1,  // 工具名不在白名单
    INVALID_ARGS = 2,  // 入参 JSON 校验失败
    TOOL_ERROR = 3,    // 工具内部失败
    TIMEOUT = 4,       // 超时
};

// 工具调用结果
struct ToolCallResult {
    ToolCallStatus status{ToolCallStatus::SUCCESS};
    Json::Value data;   // SUCCESS 时的工具结果
    std::string error;  // 失败/超时说明
};

// 工具执行器：白名单 + JSON 校验 + 同步执行（事后判超时）
class ToolExecutor {
public:
    // 注册工具（白名单 = 已注册的工具名集合）
    void registerTool(std::shared_ptr<BaseTool> tool);

    // 执行一次工具调用
    ToolCallResult execute(const std::string& name, const Json::Value& args);

    // 生成工具清单（供 Planner 拼提示词）
    Json::Value describeTools() const;

    void setTimeoutMs(int ms);

private:
    std::map<std::string, std::shared_ptr<BaseTool>> tools_;  // name -> tool
    int timeout_ms_{5000};
};

const char* toolCallStatusToString(ToolCallStatus s);

// 工厂：从 config.json 构建（读 executor.timeout_ms + tools.*.backend）
std::shared_ptr<ToolExecutor> createToolExecutor(const Json::Value& root);

}  // namespace diag
