#include "planner/llm_client.h"

#include <chrono>
#include <future>
#include <memory>
#include <thread>

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

// ---- OpenAICompatibleClient ----

OpenAICompatibleClient::OpenAICompatibleClient(const std::string& base_url, const std::string& model,
                                               const std::string& api_key, int timeout_ms,
                                               int max_retries, double temperature)
    : base_url_(base_url), model_(model), api_key_(api_key), timeout_ms_(timeout_ms),
      max_retries_(max_retries), temperature_(temperature) {}

LLMResponse OpenAICompatibleClient::chat(const std::vector<ChatMessage>& messages) {
    // 组装请求体
    Json::Value body;
    body["model"] = model_;
    body["stream"] = false;  // 非流式，直接返回完整 JSON
    body["temperature"] = temperature_;
    Json::Value msgs(Json::arrayValue);
    for (const auto& m : messages) {
        Json::Value jm;
        jm["role"] = messageRoleToString(m.role);
        jm["content"] = m.content;
        msgs.append(jm);
    }
    body["messages"] = msgs;
    std::string bodyStr = stringifyJson(body);

    std::string endpoint = base_url_ + "/chat/completions";
    std::string origin, path;
    splitUrl(endpoint, origin, path);
    auto client = drogon::HttpClient::newHttpClient(origin);

    // HTTP 瞬时失败（网络/5xx/429）重试；4xx 与解析失败不重试
    LLMResponse last;
    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->setPath(path);
        req->setBody(bodyStr);
        req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        if (!api_key_.empty()) req->addHeader("Authorization", "Bearer " + api_key_);

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
                    r.retryable = true;  // 网络失败可重试
                } else if (resp->getStatusCode() == 429 || resp->getStatusCode() >= 500) {
                    r.ok = false;
                    r.error = "LLM HTTP status " +
                              std::to_string(static_cast<int>(resp->getStatusCode()));
                    r.retryable = true;  // 限流/服务端错误可重试
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
                        r.error = "LLM response parse failed";  // 结构不对，重试无益
                    }
                }
                promise->set_value(r);
            },
            timeout_ms_ / 1000.0);

        LLMResponse r = fut.get();
        last = r;
        if (r.ok || !r.retryable) return r;
        if (attempt < max_retries_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300 * (attempt + 1)));
        }
    }
    return last;
}

// ---- MockLLMClient ----

LLMResponse MockLLMClient::chat(const std::vector<ChatMessage>& messages) {
    LLMResponse r;
    r.ok = true;
    // 首轮故意吐垃圾，演示容错重试
    if (garbage_first_ && !garbage_done_) {
        garbage_done_ = true;
        r.content = "这段输出根本不是合法的 JSON，故意模拟 LLM 出错";
        return r;
    }
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
