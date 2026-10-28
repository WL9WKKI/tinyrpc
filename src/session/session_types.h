#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <json/json.h>

namespace diag {

// 消息角色
enum class MessageRole : uint8_t {
    SYSTEM = 0,    // 系统提示词
    USER = 1,      // 用户输入 / 工具 Observation 回填
    ASSISTANT = 2, // 模型输出（Thought / Action）
};

// 一条上下文消息
struct ChatMessage {
    MessageRole role{MessageRole::USER};
    std::string content;
    int step_no{-1};  // 所属 ReAct 步；-1 = 非步骤消息（system / 首条现象）
};

// 一个任务（task_id）的完整 LLM 上下文
struct SessionContext {
    uint64_t task_id{0};
    std::vector<ChatMessage> messages;
    std::chrono::system_clock::time_point updated_at{};
};

// 截断参数
struct TruncateConfig {
    int max_tokens{4096};   // 上下文 token 上限（估算值）
    int keep_recent{4};     // 至少保留最近 N 个 ReAct 步
    int chars_per_token{4}; // token 估算：字符(字节)数 / chars_per_token
};

// 会话缓存参数
struct SessionConfig {
    std::string key_prefix{"diag:session:"};
    int ttl_seconds{3600};
    TruncateConfig truncate;
};

const char* messageRoleToString(MessageRole r);
MessageRole messageRoleFromString(const std::string& s);

// JSON 序列化（jsoncpp）
Json::Value toJson(const ChatMessage& m);
ChatMessage chatMessageFromJson(const Json::Value& v);
Json::Value toJson(const SessionContext& ctx);
SessionContext sessionContextFromJson(const Json::Value& v);

}  // namespace diag
