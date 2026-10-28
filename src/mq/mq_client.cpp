#include "mq/mq_client.h"

#include <chrono>
#include <sys/time.h>

#include <amqp.h>
#include <amqp_tcp_socket.h>

namespace diag {

// ---- MockMessageQueue ----

bool MockMessageQueue::publish(const std::string& queue, const MqMessage& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_[queue].push_back(msg);
    cv_.notify_all();
    return true;
}

bool MockMessageQueue::consume(const std::string& queue, MqMessage& out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto& q = queues_[queue];
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&q]() { return !q.empty(); })) {
        return false;
    }
    out = q.front();
    q.pop_front();
    return true;
}

void MockMessageQueue::ack(const std::string&, const MqMessage&) {}

// ---- RabbitMqClient ----

namespace {
std::string bytesToStr(amqp_bytes_t b) {
    return b.bytes ? std::string(static_cast<char*>(b.bytes), b.len) : std::string();
}
}  // namespace

RabbitMqClient::RabbitMqClient(const MqConfig& cfg) : cfg_(cfg) {
    auto* conn = amqp_new_connection();
    conn_ = conn;
    auto* socket = amqp_tcp_socket_new(conn);
    if (!socket || amqp_socket_open(socket, cfg.host.c_str(), cfg.port) != AMQP_STATUS_OK) {
        return;  // 连接失败，后续操作会失败
    }
    amqp_rpc_reply_t login = amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
                                        cfg.user.c_str(), cfg.password.c_str());
    if (login.reply_type != AMQP_RESPONSE_NORMAL) {
        return;
    }
    amqp_channel_open(conn, channel_);
    declareQueue(cfg.slow_tasks_queue);
    declareQueue(cfg.slow_results_queue);
}

RabbitMqClient::~RabbitMqClient() {
    if (conn_) {
        auto* conn = static_cast<amqp_connection_state_t>(conn_);
        amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(conn);
    }
}

bool RabbitMqClient::declareQueue(const std::string& queue) {
    auto* conn = static_cast<amqp_connection_state_t>(conn_);
    amqp_queue_declare_ok_t* r = amqp_queue_declare(
        conn, channel_, amqp_cstring_bytes(queue.c_str()), 0, 1, 0, 0, amqp_empty_table);
    return r != nullptr;
}

bool RabbitMqClient::publish(const std::string& queue, const MqMessage& msg) {
    auto* conn = static_cast<amqp_connection_state_t>(conn_);
    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CORRELATION_ID_FLAG | AMQP_BASIC_REPLY_TO_FLAG;
    props.correlation_id = amqp_cstring_bytes(msg.correlation_id.c_str());
    props.reply_to = amqp_cstring_bytes(msg.reply_to.c_str());
    int r = amqp_basic_publish(conn, channel_, amqp_empty_bytes,
                               amqp_cstring_bytes(queue.c_str()), 0, 0, &props,
                               amqp_cstring_bytes(msg.body.c_str()));
    return r == AMQP_STATUS_OK;
}

bool RabbitMqClient::consume(const std::string& queue, MqMessage& out, int timeout_ms) {
    auto* conn = static_cast<amqp_connection_state_t>(conn_);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    amqp_set_rpc_timeout(conn, &tv);

    amqp_rpc_reply_t reply = amqp_basic_get(conn, channel_, amqp_cstring_bytes(queue.c_str()), 0);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL || reply.reply.id != AMQP_BASIC_GET_OK_METHOD) {
        return false;  // 超时或空队列
    }
    auto* ok = static_cast<amqp_basic_get_ok_t*>(reply.reply.decoded);
    out.delivery_tag = ok->delivery_tag;

    amqp_message_t message;
    amqp_rpc_reply_t r2 = amqp_read_message(conn, channel_, &message, 0);
    if (r2.reply_type != AMQP_RESPONSE_NORMAL) {
        return false;
    }
    out.body = bytesToStr(message.body);
    out.correlation_id = bytesToStr(message.properties.correlation_id);
    out.reply_to = bytesToStr(message.properties.reply_to);
    amqp_destroy_message(&message);
    return true;
}

void RabbitMqClient::ack(const std::string&, const MqMessage& msg) {
    auto* conn = static_cast<amqp_connection_state_t>(conn_);
    amqp_basic_ack(conn, channel_, msg.delivery_tag, 0);
}

// ---- 工厂 ----

MqConfig parseMqConfig(const Json::Value& root) {
    MqConfig cfg;
    if (!root.isMember("mq") || !root["mq"].isObject()) return cfg;
    const Json::Value& m = root["mq"];
    if (m.isMember("backend")) cfg.backend = m["backend"].asString();
    if (m.isMember("host")) cfg.host = m["host"].asString();
    if (m.isMember("port")) cfg.port = m["port"].asInt();
    if (m.isMember("user")) cfg.user = m["user"].asString();
    if (m.isMember("password")) cfg.password = m["password"].asString();
    if (m.isMember("slow_tasks_queue")) cfg.slow_tasks_queue = m["slow_tasks_queue"].asString();
    if (m.isMember("slow_results_queue")) cfg.slow_results_queue = m["slow_results_queue"].asString();
    if (m.isMember("timeout_ms")) cfg.timeout_ms = m["timeout_ms"].asInt();
    return cfg;
}

std::shared_ptr<IMessageQueue> createMessageQueue(const Json::Value& root) {
    MqConfig cfg = parseMqConfig(root);
    if (cfg.backend == "rabbitmq") {
        return std::make_shared<RabbitMqClient>(cfg);
    }
    return std::make_shared<MockMessageQueue>();
}

}  // namespace diag
