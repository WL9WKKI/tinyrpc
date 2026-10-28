#pragma once

#include "tools/base_tool.h"

namespace diag {

// 指标查询（Prometheus）—— mock
class MockMetricTool : public BaseTool {
public:
    std::string name() const override;
    ToolType type() const override;
    std::string description() const override;
    Json::Value inputSchema() const override;
    ToolResult execute(const Json::Value& args) override;
};

// 日志查询（ELK）—— mock
class MockLogTool : public BaseTool {
public:
    std::string name() const override;
    ToolType type() const override;
    std::string description() const override;
    Json::Value inputSchema() const override;
    ToolResult execute(const Json::Value& args) override;
};

// 历史案例检索（向量检索）—— mock
class MockCaseTool : public BaseTool {
public:
    std::string name() const override;
    ToolType type() const override;
    std::string description() const override;
    Json::Value inputSchema() const override;
    ToolResult execute(const Json::Value& args) override;
};

}  // namespace diag
