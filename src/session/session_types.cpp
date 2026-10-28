#include "session/session_types.h"

namespace diag {

const char* messageRoleToString(MessageRole r) {
    switch (r) {
        case MessageRole::SYSTEM:    return "system";
        case MessageRole::USER:      return "user";
        case MessageRole::ASSISTANT: return "assistant";
    }
    return "user";
}

MessageRole messageRoleFromString(const std::string& s) {
    if (s == "system")    return MessageRole::SYSTEM;
    if (s == "assistant") return MessageRole::ASSISTANT;
    return MessageRole::USER;  // 兜底
}

Json::Value toJson(const ChatMessage& m) {
    Json::Value v;
    v["role"] = messageRoleToString(m.role);
    v["content"] = m.content;
    v["step_no"] = m.step_no;
    return v;
}

ChatMessage chatMessageFromJson(const Json::Value& v) {
    ChatMessage m;
    m.role = messageRoleFromString(v["role"].asString());
    m.content = v["content"].asString();
    m.step_no = v["step_no"].asInt();
    return m;
}

namespace {
int64_t tpToMs(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}
std::chrono::system_clock::time_point msToTp(int64_t ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}
}  // namespace

Json::Value toJson(const SessionContext& ctx) {
    Json::Value v;
    v["task_id"] = static_cast<Json::UInt64>(ctx.task_id);
    v["updated_at"] = static_cast<Json::Int64>(tpToMs(ctx.updated_at));
    Json::Value msgs(Json::arrayValue);
    for (const auto& m : ctx.messages) {
        msgs.append(toJson(m));
    }
    v["messages"] = msgs;
    return v;
}

SessionContext sessionContextFromJson(const Json::Value& v) {
    SessionContext ctx;
    ctx.task_id = v["task_id"].asUInt64();
    ctx.updated_at = msToTp(v["updated_at"].asInt64());
    for (const auto& mv : v["messages"]) {
        ctx.messages.push_back(chatMessageFromJson(mv));
    }
    return ctx;
}

}  // namespace diag
