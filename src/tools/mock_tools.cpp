#include "tools/mock_tools.h"

#include <chrono>
#include <string>
#include <thread>

namespace diag {

// ---- MockMetricTool ----

std::string MockMetricTool::name() const { return "query_metric"; }
ToolType MockMetricTool::type() const { return ToolType::METRIC; }

std::string MockMetricTool::description() const {
    return "查询指定微服务的监控指标（Prometheus），返回时间序列数据。用于判断 CPU/内存/磁盘等资源是否异常。";
}

Json::Value MockMetricTool::inputSchema() const {
    Json::Value schema;
    schema["type"] = "object";
    Json::Value props;
    props["metric"]["type"] = "string";
    props["metric"]["description"] = "指标名，如 memory_rss_bytes / cpu_usage";
    props["start"]["type"] = "string";
    props["start"]["description"] = "起始时间 RFC3339，可选";
    props["end"]["type"] = "string";
    props["step"]["type"] = "string";
    schema["properties"] = props;
    Json::Value required(Json::arrayValue);
    required.append("metric");
    schema["required"] = required;
    return schema;
}

bool MockMetricTool::isSlow() const { return slow_; }

ToolResult MockMetricTool::execute(const Json::Value& args) {
    ToolResult r;
    if (slow_) {
        std::this_thread::sleep_for(std::chrono::seconds(2));  // 模拟慢查询
    }
    if (!args.isMember("metric") || !args["metric"].isString() || args["metric"].asString().empty()) {
        r.status = ToolStatus::FAILED;
        r.error = "missing required field: metric";
        return r;
    }
    std::string metric = args["metric"].asString();
    // mock：内存泄漏场景，RSS 随时间持续上涨
    const char* ts[] = {"10:00", "10:05", "10:10", "10:15", "10:20", "10:25"};
    Json::Value points(Json::arrayValue);
    int val = 512;
    for (const char* t : ts) {
        Json::Value p;
        p["timestamp"] = t;
        p["value"] = val;
        points.append(p);
        val += 128;
    }
    Json::Value data;
    data["metric"] = metric;
    data["points"] = points;
    r.status = ToolStatus::SUCCESS;
    r.data = data;
    return r;
}

// ---- MockLogTool ----

std::string MockLogTool::name() const { return "query_log"; }
ToolType MockLogTool::type() const { return ToolType::LOG; }

std::string MockLogTool::description() const {
    return "查询指定微服务的日志（ELK），支持关键字过滤，返回匹配的日志行。用于定位异常堆栈和错误信息。";
}

Json::Value MockLogTool::inputSchema() const {
    Json::Value schema;
    schema["type"] = "object";
    Json::Value props;
    props["service"]["type"] = "string";
    props["service"]["description"] = "目标微服务名";
    props["keyword"]["type"] = "string";
    props["keyword"]["description"] = "过滤关键字，可选";
    props["start"]["type"] = "string";
    props["end"]["type"] = "string";
    props["limit"]["type"] = "integer";
    schema["properties"] = props;
    Json::Value required(Json::arrayValue);
    required.append("service");
    schema["required"] = required;
    return schema;
}

ToolResult MockLogTool::execute(const Json::Value& args) {
    ToolResult r;
    if (!args.isMember("service") || !args["service"].isString() || args["service"].asString().empty()) {
        r.status = ToolStatus::FAILED;
        r.error = "missing required field: service";
        return r;
    }
    std::string service = args["service"].asString();
    const char* ts[] = {"10:18:22", "10:21:45", "10:24:09", "10:26:33"};
    const char* lines[] = {
        "OutOfMemoryError: Java heap space",
        "GC overhead limit exceeded",
        "Container killed due to OOM, memory usage 94%",
        "memory usage 92%, threshold 90%",
    };
    Json::Value logs(Json::arrayValue);
    for (int i = 0; i < 4; ++i) {
        Json::Value l;
        l["timestamp"] = ts[i];
        l["level"] = "ERROR";
        l["message"] = lines[i];
        logs.append(l);
    }
    Json::Value data;
    data["service"] = service;
    data["logs"] = logs;
    r.status = ToolStatus::SUCCESS;
    r.data = data;
    return r;
}

// ---- MockCaseTool ----

std::string MockCaseTool::name() const { return "search_case"; }
ToolType MockCaseTool::type() const { return ToolType::CASE; }

std::string MockCaseTool::description() const {
    return "检索历史故障案例库（向量检索），输入故障现象，返回相似的历史案例及根因、修复方案。";
}

Json::Value MockCaseTool::inputSchema() const {
    Json::Value schema;
    schema["type"] = "object";
    Json::Value props;
    props["symptom"]["type"] = "string";
    props["symptom"]["description"] = "故障现象描述";
    props["top_k"]["type"] = "integer";
    props["top_k"]["description"] = "返回条数，默认 5";
    schema["properties"] = props;
    Json::Value required(Json::arrayValue);
    required.append("symptom");
    schema["required"] = required;
    return schema;
}

ToolResult MockCaseTool::execute(const Json::Value& args) {
    ToolResult r;
    if (!args.isMember("symptom") || !args["symptom"].isString() || args["symptom"].asString().empty()) {
        r.status = ToolStatus::FAILED;
        r.error = "missing required field: symptom";
        return r;
    }
    int top_k = args.isMember("top_k") ? args["top_k"].asInt() : 5;
    if (top_k < 1) top_k = 1;

    Json::Value cases(Json::arrayValue);
    Json::Value c1;
    c1["case_id"] = "CASE-1001";
    c1["title"] = "JVM 内存泄漏导致 OOM";
    c1["similarity"] = 0.93;
    c1["root_cause"] = "大对象缓存未释放，堆内存持续增长";
    c1["fix"] = "修复缓存失效逻辑，限制缓存上限";
    cases.append(c1);
    Json::Value c2;
    c2["case_id"] = "CASE-1002";
    c2["title"] = "数据库连接池泄漏";
    c2["similarity"] = 0.71;
    c2["root_cause"] = "连接未归还连接池，连接数耗尽";
    c2["fix"] = "确保连接在 finally 中归还";
    cases.append(c2);

    if (cases.size() > static_cast<Json::ArrayIndex>(top_k)) {
        cases.resize(static_cast<Json::ArrayIndex>(top_k));
    }

    Json::Value data;
    data["cases"] = cases;
    r.status = ToolStatus::SUCCESS;
    r.data = data;
    return r;
}

}  // namespace diag
