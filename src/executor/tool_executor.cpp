#include "executor/tool_executor.h"

#include <chrono>

#include "tools/mock_tools.h"

namespace diag {

const char* toolCallStatusToString(ToolCallStatus s) {
    switch (s) {
        case ToolCallStatus::SUCCESS:      return "SUCCESS";
        case ToolCallStatus::UNKNOWN_TOOL: return "UNKNOWN_TOOL";
        case ToolCallStatus::INVALID_ARGS: return "INVALID_ARGS";
        case ToolCallStatus::TOOL_ERROR:   return "TOOL_ERROR";
        case ToolCallStatus::TIMEOUT:      return "TIMEOUT";
    }
    return "UNKNOWN";
}

namespace {

// 极简 JSON Schema 校验：只支持我们工具用到的子集
//   type=object + properties[name].type + required[]
// 返回 "" 表示通过，否则返回错误描述
std::string validateArgs(const Json::Value& schema, const Json::Value& args) {
    if (!args.isObject()) {
        return "args must be a JSON object";
    }
    if (schema.isMember("required") && schema["required"].isArray()) {
        for (const auto& req : schema["required"]) {
            std::string key = req.asString();
            if (!args.isMember(key)) {
                return "missing required field: " + key;
            }
        }
    }
    if (schema.isMember("properties") && schema["properties"].isObject()) {
        const Json::Value& props = schema["properties"];
        for (const auto& key : props.getMemberNames()) {
            if (!args.isMember(key)) continue;  // 可选字段跳过
            std::string type = props[key]["type"].asString();
            const Json::Value& v = args[key];
            bool ok = true;
            if (type == "string") ok = v.isString();
            else if (type == "integer") ok = v.isInt() || v.isUInt();
            else if (type == "number") ok = v.isNumeric();
            else if (type == "boolean") ok = v.isBool();
            else if (type == "array") ok = v.isArray();
            else if (type == "object") ok = v.isObject();
            else ok = true;  // 未知类型放行
            if (!ok) return "field '" + key + "' should be " + type;
        }
    }
    return "";
}

}  // namespace

void ToolExecutor::registerTool(std::shared_ptr<BaseTool> tool) {
    if (tool) tools_[tool->name()] = std::move(tool);
}

void ToolExecutor::setTimeoutMs(int ms) {
    if (ms > 0) timeout_ms_ = ms;
}

Json::Value ToolExecutor::describeTools() const {
    Json::Value arr(Json::arrayValue);
    for (const auto& kv : tools_) {
        const auto& tool = kv.second;
        Json::Value t;
        t["name"] = tool->name();
        t["type"] = toolTypeToString(tool->type());
        t["description"] = tool->description();
        t["input_schema"] = tool->inputSchema();
        arr.append(t);
    }
    return arr;
}

ToolCallResult ToolExecutor::execute(const std::string& name, const Json::Value& args) {
    ToolCallResult res;
    // 1. 白名单
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        res.status = ToolCallStatus::UNKNOWN_TOOL;
        res.error = "unknown tool: " + name;
        return res;
    }
    // 2. JSON 校验
    std::string err = validateArgs(it->second->inputSchema(), args);
    if (!err.empty()) {
        res.status = ToolCallStatus::INVALID_ARGS;
        res.error = err;
        return res;
    }
    // 3. 同步执行 + 事后判超时
    auto start = std::chrono::steady_clock::now();
    ToolResult r = it->second->execute(args);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start).count();
    if (elapsed > timeout_ms_) {
        res.status = ToolCallStatus::TIMEOUT;
        res.error = "tool '" + name + "' exceeded " + std::to_string(timeout_ms_) + "ms";
        return res;
    }
    if (r.status == ToolStatus::FAILED) {
        res.status = ToolCallStatus::TOOL_ERROR;
        res.error = r.error;
        return res;
    }
    res.status = ToolCallStatus::SUCCESS;
    res.data = r.data;
    return res;
}

// ---- 工厂 ----

std::shared_ptr<ToolExecutor> createToolExecutor(const Json::Value& root) {
    auto exec = std::make_shared<ToolExecutor>();
    const Json::Value& e = root["executor"];
    if (e.isMember("timeout_ms") && e["timeout_ms"].isInt()) {
        exec->setTimeoutMs(e["timeout_ms"].asInt());
    }

    // 按 backend 注册工具（本期只有 mock，real 留待后续）
    const Json::Value& tools = root["tools"];
    auto backend = [](const Json::Value& c) -> std::string {
        return c.isMember("backend") ? c["backend"].asString() : "mock";
    };
    if (backend(tools["metric"]) == "mock") {
        exec->registerTool(std::make_shared<MockMetricTool>());
    }
    if (backend(tools["log"]) == "mock") {
        exec->registerTool(std::make_shared<MockLogTool>());
    }
    if (backend(tools["case"]) == "mock") {
        exec->registerTool(std::make_shared<MockCaseTool>());
    }
    return exec;
}

}  // namespace diag
