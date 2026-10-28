#include "planner/llm_client.h"

#include <future>
#include <memory>

#include <json/json.h>

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

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

bool parseJson(const std::string& s, Json::Value& out) {
    Json::CharReaderBuilder b;
    const std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}

}  // namespace

// ---- OllamaClient ----

OllamaClient::OllamaClient(const std::string& base_url, const std::string& model,
                           const std::string& api_key, int timeout_ms)
    : base_url_(base_url), model_(model), api_key_(api_key), timeout_ms_(timeout_ms) {}

LLMResponse OllamaClient::chat(const std::vector<ChatMessage>& messages) {
    LLMResponse out;
    // 组装请求体
    Json::Value body;
    body["model"] = model_;
    Json::Value msgs(Json::arrayValue);
    for (const auto& m : messages) {
        Json::Value jm;
        jm["role"] = messageRoleToString(m.role);
        jm["content"] = m.content;
        msgs.append(jm);
    }
    body["messages"] = msgs;

    std::string endpoint = base_url_ + "/chat/completions";
    std::string origin, path;
    splitUrl(endpoint, origin, path);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath(path);
    req->setBody(stringifyJson(body));
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    if (!api_key_.empty()) req->addHeader("Authorization", "Bearer " + api_key_);

    auto client = drogon::HttpClient::newHttpClient(origin);

    // 异步回调 → 同步（promise/future）
    auto promise = std::make_shared<std::promise<LLMResponse>>();
    auto fut = promise->get_future();
    client->sendRequest(
        req,
        [promise](drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
            LLMResponse r;
            if (result != drogon::ReqResult::Ok || !resp) {
                r.ok = false;
                r.error = "LLM HTTP request failed";
            } else {
                std::string raw(resp->getBody());
                Json::Value root;
                if (parseJson(raw, root) && root.isMember("choices") &&
                    root["choices"].isArray() && root["choices"].size() > 0 &&
                    root["choices"][0].isMember("message") &&
                    root["choices"][0]["message"].isMember("content")) {
                    r.ok = true;
                    r.content = root["choices"][0]["message"]["content"].asString();
                } else {
                    r.ok = false;
                    r.error = "LLM response parse failed";
                }
            }
            promise->set_value(r);
        },
        timeout_ms_ / 1000.0);

    return fut.get();
}

// ---- MockLLMClient ----

LLMResponse MockLLMClient::chat(const std::vector<ChatMessage>& messages) {
    LLMResponse r;
    r.ok = true;
    // 脚本：按已完成轮次（ASSISTANT 消息数）返回工具调用，最后给结论
    int turns = 0;
    for (const auto& m : messages) {
        if (m.role == MessageRole::ASSISTANT) ++turns;
    }
    switch (turns) {
        case 0:
            r.content = R"({"tool":"query_metric","args":{"metric":"memory_rss_bytes"}})";
            break;
        case 1:
            r.content = R"({"tool":"query_log","args":{"service":"order-service"}})";
            break;
        case 2:
            r.content = R"({"tool":"search_case","args":{"symptom":"内存持续上涨导致OOM"}})";
            break;
        default:
            r.content = R"({"answer":"根因：JVM 堆内存泄漏，大对象缓存未释放。修复：修复缓存失效逻辑并限制缓存上限。"})";
            break;
    }
    return r;
}

}  // namespace diag
