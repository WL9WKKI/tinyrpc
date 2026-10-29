#include "planner/planner.h"

#include <memory>

#include <json/json.h>

#include "metrics/metrics.h"

namespace diag {

const char* plannerActionToString(PlannerAction a) {
    switch (a) {
        case PlannerAction::CALL_TOOL: return "CALL_TOOL";
        case PlannerAction::FINISH:    return "FINISH";
        case PlannerAction::ERROR:     return "ERROR";
    }
    return "UNKNOWN";
}

namespace {

// 提取 JSON：优先 ```json ... ``` 代码块，其次首个 { 到最后一个 }
bool extractJson(const std::string& text, std::string& out) {
    size_t fence = text.find("```");
    if (fence != std::string::npos) {
        size_t start = text.find('{', fence);
        if (start != std::string::npos) {
            size_t end = text.rfind('}');
            if (end != std::string::npos && end > start) {
                out = text.substr(start, end - start + 1);
                return true;
            }
        }
    }
    size_t start = text.find('{');
    size_t end = text.rfind('}');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        out = text.substr(start, end - start + 1);
        return true;
    }
    return false;
}

bool parseJson(const std::string& s, Json::Value& out) {
    Json::CharReaderBuilder b;
    const std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}

std::string stringifyJson(const Json::Value& v) {
    return Json::writeString(Json::StreamWriterBuilder(), v);
}

}  // namespace

Planner::Planner(std::shared_ptr<ILLMClient> client, const Json::Value& tools, int max_retries)
    : client_(std::move(client)), tools_(tools), max_retries_(max_retries) {
    system_prompt_ = buildSystemPrompt();
}

std::string Planner::buildSystemPrompt() const {
    std::string s =
        "你是一名资深微服务故障诊断工程师。根据故障现象，逐步调用工具收集证据，最终定位根因。\n\n"
        "可用工具：\n" + stringifyJson(tools_) +
        "\n\n你必须只输出一个 JSON 对象，不要输出任何其他文字或解释：\n"
        "- 需要查数据时：{\"tool\":\"<工具名>\",\"args\":{...}}\n"
        "- 已能下结论时：{\"answer\":\"<最终诊断结论>\"}\n\n"
        "示例1（查指标）：{\"tool\":\"query_metric\",\"args\":{\"metric\":\"memory_rss_bytes\"}}\n"
        "示例2（下结论）：{\"answer\":\"根因是 JVM 堆内存泄漏，建议修复缓存失效逻辑\"}";
    return s;
}

PlanResult Planner::plan(const std::vector<ChatMessage>& messages) {
    PlanResult res;
    // 组装请求：Planner 自己的 system 提示词 + 跳过原会话里的 system
    std::vector<ChatMessage> req;
    req.reserve(messages.size() + 1);
    req.push_back({MessageRole::SYSTEM, system_prompt_, -1});
    for (const auto& m : messages) {
        if (m.role == MessageRole::SYSTEM) continue;
        req.push_back(m);
    }

    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        LLMResponse r;
        {
            ScopedTimer t(metrics_.get(), "llm");
            r = client_->chat(req);
        }
        if (!r.ok) {
            res.action = PlannerAction::ERROR;
            res.error = r.error;
            return res;
        }
        std::string json;
        if (!extractJson(r.content, json)) {
            req.push_back({MessageRole::USER, "你的输出里没有 JSON，请只输出一个 JSON 对象。", -1});
            continue;  // 无 JSON 也重试，而非直接失败
        }
        Json::Value root;
        if (parseJson(json, root)) {
            if (root.isMember("tool") && root["tool"].isString()) {
                res.action = PlannerAction::CALL_TOOL;
                res.call.tool = root["tool"].asString();
                res.call.args = root.isMember("args") ? root["args"]
                                                      : Json::Value(Json::objectValue);
                return res;
            }
            if (root.isMember("answer") && root["answer"].isString()) {
                res.action = PlannerAction::FINISH;
                res.answer = root["answer"].asString();
                return res;
            }
            // JSON 合法但缺少 tool/answer → 纠错重试
            req.push_back({MessageRole::USER, "输出缺少 tool 或 answer 字段，请重新输出 JSON。", -1});
        } else {
            // JSON 非法 → 纠错重试
            req.push_back({MessageRole::USER, "上一条输出不是合法 JSON，请只输出一个 JSON 对象。", -1});
        }
    }
    res.action = PlannerAction::ERROR;
    res.error = "解析失败，重试 " + std::to_string(max_retries_) + " 次后放弃";
    return res;
}

// ---- 工厂 ----

std::shared_ptr<Planner> createPlanner(const Json::Value& root, const Json::Value& tools) {
    const Json::Value& agent = root["agent"];
    const Json::Value& llm = agent["llm"];
    std::string backend = llm.isMember("backend") ? llm["backend"].asString() : "mock";
    int max_retries = agent.isMember("max_retries") ? agent["max_retries"].asInt() : 3;

    std::shared_ptr<ILLMClient> client;
    if (backend == "openai") {
        std::string base_url = llm.isMember("base_url") ? llm["base_url"].asString()
                                                        : "https://api.deepseek.com/v1";
        std::string model = llm.isMember("model") ? llm["model"].asString() : "deepseek-chat";
        std::string api_key = llm.isMember("api_key") ? llm["api_key"].asString() : "";
        int timeout_ms = llm.isMember("timeout_ms") ? llm["timeout_ms"].asInt() : 30000;
        double temperature = llm.isMember("temperature") ? llm["temperature"].asDouble() : 0.0;
        client = std::make_shared<OpenAICompatibleClient>(base_url, model, api_key, timeout_ms,
                                                          max_retries, temperature);
    } else {
        bool garbage_first = llm.isMember("garbage_first") ? llm["garbage_first"].asBool() : false;
        client = std::make_shared<MockLLMClient>(garbage_first);
    }
    return std::make_shared<Planner>(client, tools, max_retries);
}

void Planner::setMetricsRegistry(std::shared_ptr<MetricsRegistry> metrics) {
    metrics_ = std::move(metrics);
}

}  // namespace diag
