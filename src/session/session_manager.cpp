#include "session/session_manager.h"

#include <algorithm>
#include <chrono>
#include <set>

#include "session/session_store.h"

namespace diag {

namespace {
const char* kDefaultSystemPrompt =
    "你是一名微服务故障诊断助手。根据给定的故障现象，逐步推理并调用工具定位根因。";

std::string jsonStr(const Json::Value& v, const char* key, const char* def) {
    const Json::Value& x = v[key];
    return x.isString() ? x.asString() : def;
}
int jsonInt(const Json::Value& v, const char* key, int def) {
    const Json::Value& x = v[key];
    return x.isInt() ? x.asInt() : def;
}
bool jsonBool(const Json::Value& v, const char* key, bool def) {
    const Json::Value& x = v[key];
    return x.isBool() ? x.asBool() : def;
}
}  // namespace

SessionManager::SessionManager(std::shared_ptr<ISessionStore> store, SessionConfig cfg)
    : store_(std::move(store)), cfg_(std::move(cfg)) {}

SessionContext SessionManager::getOrCreate(const DiagnosisTask& task) {
    SessionContext ctx;
    if (store_->load(task.task_id, ctx)) return ctx;
    // 未命中：初始化 system 提示词 + 首条现象
    ctx.task_id = task.task_id;
    ctx.messages.push_back({MessageRole::SYSTEM, kDefaultSystemPrompt, -1});
    // 首条 USER 消息带上服务名，避免后续 query_log 缺 service
    std::string symptom = task.symptom;
    if (!task.service_name.empty()) {
        symptom = "服务 " + task.service_name + " 故障现象：" + task.symptom;
    }
    ctx.messages.push_back({MessageRole::USER, symptom, -1});
    ctx.updated_at = std::chrono::system_clock::now();
    store_->save(ctx);
    return ctx;
}

void SessionManager::append(uint64_t task_id, MessageRole role,
                            const std::string& content, int step_no) {
    SessionContext ctx;
    if (!store_->load(task_id, ctx)) ctx.task_id = task_id;
    ctx.messages.push_back({role, content, step_no});
    ctx.updated_at = std::chrono::system_clock::now();
    truncate(ctx);
    store_->save(ctx);
}

void SessionManager::appendStep(uint64_t task_id, const ReactStep& step) {
    SessionContext ctx;
    if (!store_->load(task_id, ctx)) ctx.task_id = task_id;

    // ASSISTANT: thought（+ action）
    std::string assistant = step.thought;
    if (!step.action.empty()) {
        assistant += "\nAction: " + step.action;
        if (!step.action_input.empty()) assistant += "(" + step.action_input + ")";
    }
    ctx.messages.push_back({MessageRole::ASSISTANT, assistant, step.step_no});
    // USER: observation
    ctx.messages.push_back({MessageRole::USER, step.observation, step.step_no});

    ctx.updated_at = std::chrono::system_clock::now();
    truncate(ctx);
    store_->save(ctx);
}

int SessionManager::estimateTokens(const SessionContext& ctx) const {
    int cpt = cfg_.truncate.chars_per_token;
    if (cpt <= 0) cpt = 4;
    int total = 0;
    for (const auto& m : ctx.messages) {
        total += (static_cast<int>(m.content.size()) + cpt - 1) / cpt;  // 向上取整
    }
    return total;
}

void SessionManager::truncate(SessionContext& ctx) {
    const TruncateConfig& tc = cfg_.truncate;
    if (estimateTokens(ctx) <= tc.max_tokens) return;

    // 收集所有步骤号（step_no >= 0）
    std::set<int> stepNos;
    for (const auto& m : ctx.messages) {
        if (m.step_no >= 0) stepNos.insert(m.step_no);
    }

    // 最近 keep_recent 个步骤保留
    std::set<int> keepSteps;
    int k = tc.keep_recent;
    for (auto it = stepNos.rbegin(); it != stepNos.rend() && k > 0; ++it, --k) {
        keepSteps.insert(*it);
    }

    // 从最旧的步骤整组丢弃
    for (int sn : stepNos) {
        if (keepSteps.count(sn)) continue;
        ctx.messages.erase(
            std::remove_if(ctx.messages.begin(), ctx.messages.end(),
                           [sn](const ChatMessage& m) { return m.step_no == sn; }),
            ctx.messages.end());
        if (estimateTokens(ctx) <= tc.max_tokens) break;
    }
    // 若「头 + 最近 N 步」仍超限，不再强删（由配置保证 max_tokens 足够）
}

std::vector<ChatMessage> SessionManager::buildMessages(const SessionContext& ctx) const {
    return ctx.messages;
}

void SessionManager::clear(uint64_t task_id) {
    store_->erase(task_id);
}

// ---- 工厂：从 config.json 构建 ----

std::shared_ptr<SessionManager> createSessionManager(const Json::Value& root) {
    SessionConfig cfg;
    const Json::Value& s = root["session"];
    cfg.key_prefix = jsonStr(s, "key_prefix", "diag:session:");
    cfg.ttl_seconds = jsonInt(s, "ttl_seconds", 3600);
    const Json::Value& t = s["truncate"];
    cfg.truncate.max_tokens = jsonInt(t, "max_tokens", 4096);
    cfg.truncate.keep_recent = jsonInt(t, "keep_recent", 4);
    cfg.truncate.chars_per_token = jsonInt(t, "chars_per_token", 4);

    std::shared_ptr<ISessionStore> store;
    const Json::Value& r = root["redis"];
    if (jsonBool(r, "enabled", false)) {
        store = std::make_shared<RedisSessionStore>(
            jsonStr(r, "host", "127.0.0.1"),
            jsonInt(r, "port", 6379),
            jsonStr(r, "password", ""),
            jsonInt(r, "db", 0),
            cfg.key_prefix, cfg.ttl_seconds);
    } else {
        store = std::make_shared<MockSessionStore>();
    }
    return std::make_shared<SessionManager>(store, cfg);
}

}  // namespace diag
