#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <json/json.h>

#include "model/types.h"
#include "session/session_types.h"

namespace diag {

class ISessionStore;

// 会话管理：上下文读取/追加/截断，底层经 ISessionStore 缓存
class SessionManager {
public:
    SessionManager(std::shared_ptr<ISessionStore> store, SessionConfig cfg);

    // 取上下文：缓存命中则用缓存，否则用 system + 首条现象初始化
    SessionContext getOrCreate(const DiagnosisTask& task);

    // 追加一个 ReAct 步（Thought/Action/Observation）
    void appendStep(uint64_t task_id, const ReactStep& step);

    // 截断到 token 上限（HEAD_TAIL：保留头 + 最近 N 步）
    void truncate(SessionContext& ctx);

    // 组装成 LLM 消息数组（截断后）
    std::vector<ChatMessage> buildMessages(const SessionContext& ctx) const;

    // 清理会话缓存
    void clear(uint64_t task_id);

private:
    void append(uint64_t task_id, MessageRole role, const std::string& content, int step_no);
    int estimateTokens(const SessionContext& ctx) const;

    std::shared_ptr<ISessionStore> store_;
    SessionConfig cfg_;
};

// 从 config.json 构建（读取 redis / session 段）
std::shared_ptr<SessionManager> createSessionManager(const Json::Value& root);

}  // namespace diag
