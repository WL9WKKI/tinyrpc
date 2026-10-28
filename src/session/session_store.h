#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "session/session_types.h"

struct redisContext;  // hiredis 前置声明

namespace diag {

// 会话缓存适配接口（mock / 真实 Redis 可切换）
class ISessionStore {
public:
    virtual ~ISessionStore() = default;
    virtual bool load(uint64_t task_id, SessionContext& out) = 0;  // 命中 true
    virtual void save(const SessionContext& ctx) = 0;
    virtual void erase(uint64_t task_id) = 0;
};

// 内存实现（redis.enabled=false 时使用）
class MockSessionStore : public ISessionStore {
public:
    bool load(uint64_t task_id, SessionContext& out) override;
    void save(const SessionContext& ctx) override;
    void erase(uint64_t task_id) override;

private:
    std::mutex mutex_;
    std::unordered_map<uint64_t, SessionContext> store_;
};

// 真实 Redis 实现（hiredis 同步客户端）
class RedisSessionStore : public ISessionStore {
public:
    RedisSessionStore(const std::string& host, int port, const std::string& password, int db,
                      const std::string& key_prefix, int ttl_seconds);
    ~RedisSessionStore() override;

    bool load(uint64_t task_id, SessionContext& out) override;
    void save(const SessionContext& ctx) override;
    void erase(uint64_t task_id) override;

private:
    std::string keyOf(uint64_t task_id) const;
    bool ensureConnected();

    std::string host_;
    int port_;
    std::string password_;
    int db_;
    std::string key_prefix_;
    int ttl_seconds_;

    redisContext* ctx_{nullptr};
    std::mutex mutex_;
};

}  // namespace diag
