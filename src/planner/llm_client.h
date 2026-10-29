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
    bool retryable{false};  // 瞬时失败（网络/5xx/429）可重试；4xx/解析失败不可重试
};

// LLM 客户端接口（mock / OpenAI 兼容可切换）
class ILLMClient {
public:
    virtual ~ILLMClient() = default;
    virtual LLMResponse chat(const std::vector<ChatMessage>& messages) = 0;
};

// 真实 LLM（OpenAI 兼容 /v1/chat/completions，DeepSeek/Qwen/Ollama 通用），经 Drogon HttpClient
class OpenAICompatibleClient : public ILLMClient {
public:
    OpenAICompatibleClient(const std::string& base_url, const std::string& model,
                           const std::string& api_key, int timeout_ms,
                           int max_retries = 0, double temperature = 0.0);

    LLMResponse chat(const std::vector<ChatMessage>& messages) override;

private:
    std::string base_url_;
    std::string model_;
    std::string api_key_;
    int timeout_ms_;
    int max_retries_{0};  // HTTP 瞬时失败（网络/5xx/429）重试次数
    double temperature_;
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
