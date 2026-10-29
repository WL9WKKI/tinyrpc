#include "tools/real_tools.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace diag {

namespace {

// 解析 "http://host:port/path" → origin 与 path
void splitUrl(const std::string& url, std::string& origin, std::string& path) {
    size_t schemeEnd = url.find("://");
    size_t pos = (schemeEnd == std::string::npos) ? 0 : schemeEnd + 3;
    size_t slash = url.find('/', pos);
    if (slash == std::string::npos) {
        origin = url;
        path = "/";
    } else {
        origin = url.substr(0, slash);
        path = url.substr(slash);
    }
}

std::string stringifyJson(const Json::Value& v) {
    return Json::writeString(Json::StreamWriterBuilder(), v);
}

// 简单百分号编码（把 PromQL 安全拼进 query 参数）
std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

struct HttpJsonResult {
    bool ok{false};
    std::string error;
    Json::Value json;
    bool retryable{false};  // 瞬时失败（网络/5xx/429）可重试
};

// 同步 HTTP 请求（GET/POST），期望返回 JSON。retries 为瞬时失败重试次数。
// 注意：依赖 Drogon 事件循环，只能在事件循环线程内调用（快路径）。
HttpJsonResult httpJson(const std::string& method, const std::string& url,
                        const std::string& body, int timeout_ms, int retries) {
    std::string origin, path;
    splitUrl(url, origin, path);
    auto client = drogon::HttpClient::newHttpClient(origin);

    HttpJsonResult last;
    for (int attempt = 0; attempt <= retries; ++attempt) {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(method == "POST" ? drogon::Post : drogon::Get);
        req->setPath(path);
        if (!body.empty()) {
            req->setBody(body);
            req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        }

        auto promise = std::make_shared<std::promise<HttpJsonResult>>();
        auto fut = promise->get_future();
        client->sendRequest(
            req,
            [promise](drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
                HttpJsonResult r;
                if (result != drogon::ReqResult::Ok || !resp) {
                    r.error = "HTTP request failed";
                    r.retryable = true;  // 网络失败可重试
                    promise->set_value(r);
                    return;
                }
                if (resp->getStatusCode() == 429 || resp->getStatusCode() >= 500) {
                    r.error = "HTTP status " + std::to_string(static_cast<int>(resp->getStatusCode()));
                    r.retryable = true;  // 限流/服务端错误可重试
                    promise->set_value(r);
                    return;
                }
                if (resp->getStatusCode() != drogon::k200OK) {
                    r.error = "HTTP status " + std::to_string(static_cast<int>(resp->getStatusCode()));
                    promise->set_value(r);  // 4xx 等，不重试
                    return;
                }
                std::string raw(resp->getBody());
                Json::CharReaderBuilder b;
                std::unique_ptr<Json::CharReader> reader(b.newCharReader());
                std::string errs;
                if (!reader->parse(raw.data(), raw.data() + raw.size(), &r.json, &errs)) {
                    r.error = "response parse failed";  // 解析失败，不重试
                    promise->set_value(r);
                    return;
                }
                r.ok = true;
                promise->set_value(r);
            },
            timeout_ms / 1000.0);

        HttpJsonResult r = fut.get();
        last = r;
        if (r.ok || !r.retryable) return r;
        if (attempt < retries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
        }
    }
    return last;
}

}  // namespace

// ---- 配置解析 ----

PrometheusConfig parsePrometheusConfig(const Json::Value& metricCfg) {
    PrometheusConfig cfg;
    if (metricCfg.isMember("base_url") && metricCfg["base_url"].isString()) {
        cfg.base_url = metricCfg["base_url"].asString();
    }
    if (metricCfg.isMember("timeout_ms") && metricCfg["timeout_ms"].isInt()) {
        cfg.timeout_ms = metricCfg["timeout_ms"].asInt();
    }
    if (metricCfg.isMember("retries") && metricCfg["retries"].isInt()) {
        cfg.retries = metricCfg["retries"].asInt();
    }
    return cfg;
}

ElasticsearchConfig parseEsConfig(const Json::Value& logCfg) {
    ElasticsearchConfig cfg;
    if (logCfg.isMember("base_url") && logCfg["base_url"].isString()) {
        cfg.base_url = logCfg["base_url"].asString();
    }
    if (logCfg.isMember("index") && logCfg["index"].isString()) {
        cfg.index = logCfg["index"].asString();
    }
    if (logCfg.isMember("timeout_ms") && logCfg["timeout_ms"].isInt()) {
        cfg.timeout_ms = logCfg["timeout_ms"].asInt();
    }
    if (logCfg.isMember("retries") && logCfg["retries"].isInt()) {
        cfg.retries = logCfg["retries"].asInt();
    }
    return cfg;
}

// ---- RealMetricTool ----

std::string RealMetricTool::name() const { return "query_metric"; }
ToolType RealMetricTool::type() const { return ToolType::METRIC; }

std::string RealMetricTool::description() const {
    return "查询指定微服务的监控指标（Prometheus），返回时间序列数据。用于判断 CPU/内存/磁盘等资源是否异常。";
}

Json::Value RealMetricTool::inputSchema() const {
    Json::Value schema;
    schema["type"] = "object";
    Json::Value props;
    props["metric"]["type"] = "string";
    props["metric"]["description"] = "指标名，如 memory_rss_bytes / cpu_usage";
    props["start"]["type"] = "string";
    props["start"]["description"] = "起始时间 RFC3339，可选";
    props["end"]["type"] = "string";
    props["end"]["description"] = "结束时间 RFC3339，可选";
    props["step"]["type"] = "string";
    schema["properties"] = props;
    Json::Value required(Json::arrayValue);
    required.append("metric");
    schema["required"] = required;
    return schema;
}

ToolResult RealMetricTool::execute(const Json::Value& args) {
    ToolResult r;
    if (!args.isMember("metric") || !args["metric"].isString() || args["metric"].asString().empty()) {
        r.status = ToolStatus::FAILED;
        r.error = "missing required field: metric";
        return r;
    }
    std::string metric = args["metric"].asString();

    // PromQL：指标名 + 可选 service 标签过滤
    std::string promql = metric;
    if (args.isMember("service") && args["service"].isString() && !args["service"].asString().empty()) {
        promql += "{service=\"" + args["service"].asString() + "\"}";
    }

    // 时间窗：最近 5 分钟、step 15 秒（step 须大于抓取间隔，避免相邻点复用同一样本产生平台期）
    long long end = static_cast<long long>(std::time(nullptr));
    long long start = end - 5 * 60;
    int step = 15;

    std::string url = cfg_.base_url + "/api/v1/query_range?query=" + urlEncode(promql) +
                      "&start=" + std::to_string(start) +
                      "&end=" + std::to_string(end) +
                      "&step=" + std::to_string(step);

    HttpJsonResult h = httpJson("GET", url, "", cfg_.timeout_ms, cfg_.retries);
    if (!h.ok) {
        r.status = ToolStatus::FAILED;
        r.error = "Prometheus 查询失败: " + h.error;
        return r;
    }

    // data.result[].values（二维 [unix, "value"]）→ points
    Json::Value points(Json::arrayValue);
    if (h.json.isMember("data") && h.json["data"].isMember("result") &&
        h.json["data"]["result"].isArray()) {
        const Json::Value& result = h.json["data"]["result"];
        if (!result.empty() && result[0].isMember("values") && result[0]["values"].isArray()) {
            for (const auto& v : result[0]["values"]) {
                if (!v.isArray() || v.size() < 2) continue;
                double val = 0.0;
                if (v[1].isString()) val = std::stod(v[1].asString());
                else if (v[1].isNumeric()) val = v[1].asDouble();
                Json::Value p;
                p["timestamp"] = std::to_string(v[0].asInt64());
                p["value"] = val;
                points.append(p);
            }
        }
    }

    Json::Value data;
    data["metric"] = metric;
    data["points"] = points;
    r.status = ToolStatus::SUCCESS;
    r.data = data;
    return r;
}

// ---- RealLogTool ----

std::string RealLogTool::name() const { return "query_log"; }
ToolType RealLogTool::type() const { return ToolType::LOG; }

std::string RealLogTool::description() const {
    return "查询指定微服务的日志（ELK），支持关键字过滤，返回匹配的日志行。用于定位异常堆栈和错误信息。";
}

Json::Value RealLogTool::inputSchema() const {
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

ToolResult RealLogTool::execute(const Json::Value& args) {
    ToolResult r;
    if (!args.isMember("service") || !args["service"].isString() || args["service"].asString().empty()) {
        r.status = ToolStatus::FAILED;
        r.error = "missing required field: service";
        return r;
    }
    std::string service = args["service"].asString();
    int limit = args.isMember("limit") ? args["limit"].asInt() : 10;
    if (limit < 1) limit = 1;

    // ES query：service 精确匹配 + 可选 keyword 匹配 message
    Json::Value must(Json::arrayValue);
    Json::Value termService;
    termService["term"]["service"] = service;
    must.append(termService);
    if (args.isMember("keyword") && args["keyword"].isString() && !args["keyword"].asString().empty()) {
        Json::Value matchMsg;
        matchMsg["match"]["message"] = args["keyword"].asString();
        must.append(matchMsg);
    }
    Json::Value query;
    query["bool"]["must"] = must;

    Json::Value sortObj;
    sortObj["@timestamp"] = "desc";
    Json::Value sort(Json::arrayValue);
    sort.append(sortObj);

    Json::Value body;
    body["query"] = query;
    body["size"] = limit;
    body["sort"] = sort;

    std::string url = cfg_.base_url + "/" + cfg_.index + "/_search";
    HttpJsonResult h = httpJson("POST", url, stringifyJson(body), cfg_.timeout_ms, cfg_.retries);
    if (!h.ok) {
        r.status = ToolStatus::FAILED;
        r.error = "ES 查询失败: " + h.error;
        return r;
    }

    // hits.hits[]._source → logs
    Json::Value logs(Json::arrayValue);
    if (h.json.isMember("hits") && h.json["hits"].isMember("hits") && h.json["hits"]["hits"].isArray()) {
        for (const auto& hit : h.json["hits"]["hits"]) {
            if (!hit.isMember("_source")) continue;
            const Json::Value& src = hit["_source"];
            Json::Value l;
            l["timestamp"] = src.isMember("@timestamp") ? src["@timestamp"].asString() : "";
            l["level"] = src.isMember("level") ? src["level"].asString() : "";
            l["message"] = src.isMember("message") ? src["message"].asString() : "";
            logs.append(l);
        }
    }

    Json::Value data;
    data["service"] = service;
    data["logs"] = logs;
    r.status = ToolStatus::SUCCESS;
    r.data = data;
    return r;
}

}  // namespace diag
