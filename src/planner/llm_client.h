#pragma once

#include <string>
#include <vector>

#include "session/session_types.h"

namespace diag {

// LLM 原始响应
struct LLMResponse {
    bool ok{false};
    std::string content;  // 输出文本
    std::string error;    // 失败原因
};

// LLM 客户端接口（mock / ollama 可切换）
class ILLMClient {
public:
    virtual ~ILLMClient() = default;
    virtual LLMResponse chat(const std::vector<ChatMessage>& messages) = 0;
};

// 真实 ollama（OpenAI 兼容 /v1/chat/completions），经 Drogon HttpClient
class OllamaClient : public ILLMClient {
public:
    OllamaClient(const std::string& base_url, const std::string& model,
                 const std::string& api_key, int timeout_ms);

    LLMResponse chat(const std::vector<ChatMessage>& messages) override;

private:
    std::string base_url_;
    std::string model_;
    std::string api_key_;
    int timeout_ms_;
};

// mock：返回脚本化回复（测闭环/重试，不开 ollama）
// garbage_first=true 时首轮故意吐非法 JSON，演示容错重试
class MockLLMClient : public ILLMClient {
public:
    explicit MockLLMClient(bool garbage_first = false) : garbage_first_(garbage_first) {}

    LLMResponse chat(const std::vector<ChatMessage>& messages) override;

private:
    bool garbage_first_{false};
    bool garbage_done_{false};
};

}  // namespace diag
