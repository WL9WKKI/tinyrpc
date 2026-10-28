#include "planner/planner.h"

#include <memory>

#include <json/json.h>

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
        "你是一名微服务故障诊断助手。根据故障现象，逐步调用工具收集证据并定位根因。\n\n"
        "可用工具：\n" + stringifyJson(tools_) +
        "\n\n规则：\n"
        "1. 需要查数据时，输出：{\"tool\":\"<工具名>\",\"args\":{...}}\n"
        "2. 已能下结论时，输出：{\"answer\":\"<最终诊断结论>\"}\n"
        "3. 只输出一个 JSON 对象，不要输出任何其他文字。";
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
        LLMResponse r = client_->chat(req);
        if (!r.ok) {
            res.action = PlannerAction::ERROR;
            res.error = r.error;
            return res;
        }
        std::string json;
        if (!extractJson(r.content, json)) {
            res.action = PlannerAction::ERROR;
            res.error = "no JSON found in LLM output";
            return res;
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
    if (backend == "ollama") {
        std::string base_url = llm.isMember("base_url") ? llm["base_url"].asString()
                                                        : "http://127.0.0.1:11434/v1";
        std::string model = llm.isMember("model") ? llm["model"].asString() : "qwen2.5:7b";
        std::string api_key = llm.isMember("api_key") ? llm["api_key"].asString() : "";
        int timeout_ms = llm.isMember("timeout_ms") ? llm["timeout_ms"].asInt() : 30000;
        client = std::make_shared<OllamaClient>(base_url, model, api_key, timeout_ms);
    } else {
        client = std::make_shared<MockLLMClient>();
    }
    return std::make_shared<Planner>(client, tools, max_retries);
}

}  // namespace diag
