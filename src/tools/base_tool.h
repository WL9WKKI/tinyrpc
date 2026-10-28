#pragma once

#include <cstdint>
#include <string>

#include <json/json.h>

namespace diag {

// 工具类型（对应三类外部下游）
enum class ToolType : uint8_t {
    METRIC = 0,  // Prometheus 指标
    LOG    = 1,  // ELK 日志
    CASE   = 2,  // 历史案例检索（向量检索）
};

// 工具执行结果状态
enum class ToolStatus : uint8_t {
    SUCCESS = 0,  // 成功
    FAILED  = 1,  // 失败（入参非法/内部错误）
    TIMEOUT = 2,  // 超时（本期工具不产生，留给 ToolExecutor）
};

// 工具执行结果
struct ToolResult {
    ToolStatus status{ToolStatus::SUCCESS};
    Json::Value data;   // 成功时的结构化结果
    std::string error;  // 失败原因
};

// 工具基类：所有诊断工具（mock / 真实）的统一接口
class BaseTool {
public:
    virtual ~BaseTool() = default;

    virtual std::string name() const = 0;          // 唯一标识
    virtual ToolType type() const = 0;             // 工具类型
    virtual std::string description() const = 0;   // 给 LLM 的说明
    virtual Json::Value inputSchema() const = 0;   // 入参 JSON Schema

    // 是否为慢工具（慢工具走 MQ 异步路径）
    virtual bool isSlow() const { return false; }

    // 同步执行，错误不走异常，进 ToolResult::error
    virtual ToolResult execute(const Json::Value& args) = 0;
};

const char* toolTypeToString(ToolType t);
const char* toolStatusToString(ToolStatus s);

}  // namespace diag
