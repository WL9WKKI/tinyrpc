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
class MockLLMClient : public ILLMClient {
public:
    LLMResponse chat(const std::vector<ChatMessage>& messages) override;
};

}  // namespace diag
