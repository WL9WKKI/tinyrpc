#include "session/session_store.h"

#include <memory>
#include <sys/time.h>

#include <hiredis/hiredis.h>
#include <json/json.h>

namespace diag {

// ---- MockSessionStore ----

bool MockSessionStore::load(uint64_t task_id, SessionContext& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(task_id);
    if (it == store_.end()) return false;
    out = it->second;
    return true;
}

void MockSessionStore::save(const SessionContext& ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_[ctx.task_id] = ctx;
}

void MockSessionStore::erase(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.erase(task_id);
}

// ---- RedisSessionStore ----

namespace {
std::string stringifyJson(const Json::Value& v) {
    return Json::writeString(Json::StreamWriterBuilder(), v);
}
bool parseJson(const std::string& s, Json::Value& out) {
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}
}  // namespace

RedisSessionStore::RedisSessionStore(const std::string& host, int port,
                                     const std::string& password, int db,
                                     const std::string& key_prefix, int ttl_seconds)
    : host_(host), port_(port), password_(password), db_(db),
      key_prefix_(key_prefix), ttl_seconds_(ttl_seconds) {}

RedisSessionStore::~RedisSessionStore() {
    if (ctx_) redisFree(ctx_);
}

std::string RedisSessionStore::keyOf(uint64_t task_id) const {
    return key_prefix_ + std::to_string(task_id);
}

bool RedisSessionStore::ensureConnected() {
    if (ctx_ != nullptr) return true;
    struct timeval timeout{1, 0};
    ctx_ = redisConnectWithTimeout(host_.c_str(), port_, timeout);
    if (ctx_ == nullptr || ctx_->err) {
        if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }
        return false;
    }
    if (!password_.empty()) {
        auto* r = static_cast<redisReply*>(
            redisCommand(ctx_, "AUTH %b", password_.data(), password_.size()));
        if (r == nullptr || r->type == REDIS_REPLY_ERROR) {
            if (r) freeReplyObject(r);
            redisFree(ctx_); ctx_ = nullptr;
            return false;
        }
        freeReplyObject(r);
    }
    if (db_ != 0) {
        auto* r = static_cast<redisReply*>(redisCommand(ctx_, "SELECT %d", db_));
        if (r == nullptr || r->type == REDIS_REPLY_ERROR) {
            if (r) freeReplyObject(r);
            redisFree(ctx_); ctx_ = nullptr;
            return false;
        }
        freeReplyObject(r);
    }
    return true;
}

bool RedisSessionStore::load(uint64_t task_id, SessionContext& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    std::string key = keyOf(task_id);
    auto* r = static_cast<redisReply*>(redisCommand(ctx_, "GET %s", key.c_str()));
    if (r == nullptr) { redisFree(ctx_); ctx_ = nullptr; return false; }  // 连接错误
    bool hit = false;
    if (r->type == REDIS_REPLY_STRING) {
        Json::Value root;
        if (parseJson(std::string(r->str, r->len), root)) {
            out = sessionContextFromJson(root);
            hit = true;
        }
    }
    freeReplyObject(r);
    return hit;
}

void RedisSessionStore::save(const SessionContext& ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return;
    std::string key = keyOf(ctx.task_id);
    std::string value = stringifyJson(toJson(ctx));
    auto* r = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %b EX %d", key.c_str(), value.data(), value.size(), ttl_seconds_));
    if (r == nullptr) { redisFree(ctx_); ctx_ = nullptr; }
    else freeReplyObject(r);
}

void RedisSessionStore::erase(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return;
    std::string key = keyOf(task_id);
    auto* r = static_cast<redisReply*>(redisCommand(ctx_, "DEL %s", key.c_str()));
    if (r == nullptr) { redisFree(ctx_); ctx_ = nullptr; }
    else freeReplyObject(r);
}

}  // namespace diag
