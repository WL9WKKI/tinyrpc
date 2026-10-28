#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <json/json.h>

namespace diag {

// MQ 配置
struct MqConfig {
    std::string backend{"mock"};  // mock | rabbitmq
    std::string host{"127.0.0.1"};
    int port{5672};
    std::string user{"guest"};
    std::string password{"guest"};
    std::string slow_tasks_queue{"diag.slow-tasks"};
    std::string slow_results_queue{"diag.slow-results"};
    int timeout_ms{30000};
};

// 一条消息
struct MqMessage {
    std::string body;
    std::string correlation_id;
    std::string reply_to;
    uint64_t delivery_tag{0};  // RabbitMQ ack 用
};

// 消息队列接口（mock / RabbitMQ 可切换）
class IMessageQueue {
public:
    virtual ~IMessageQueue() = default;
    // 发布到队列
    virtual bool publish(const std::string& queue, const MqMessage& msg) = 0;
    // 从队列取一条（阻塞，超时返回 false）
    virtual bool consume(const std::string& queue, MqMessage& out, int timeout_ms) = 0;
    // 确认消息
    virtual void ack(const std::string& queue, const MqMessage& msg) = 0;
};

// 内存实现（mq.backend=mock）
class MockMessageQueue : public IMessageQueue {
public:
    bool publish(const std::string& queue, const MqMessage& msg) override;
    bool consume(const std::string& queue, MqMessage& out, int timeout_ms) override;
    void ack(const std::string& queue, const MqMessage& msg) override;

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, std::deque<MqMessage>> queues_;
};

// RabbitMQ 实现（mq.backend=rabbitmq，librabbitmq-c）
class RabbitMqClient : public IMessageQueue {
public:
    explicit RabbitMqClient(const MqConfig& cfg);
    ~RabbitMqClient() override;

    bool publish(const std::string& queue, const MqMessage& msg) override;
    bool consume(const std::string& queue, MqMessage& out, int timeout_ms) override;
    void ack(const std::string& queue, const MqMessage& msg) override;

private:
    bool declareQueue(const std::string& queue);

    MqConfig cfg_;
    void* conn_{nullptr};  // amqp_connection_state_t
    int channel_{1};
};

// 工厂 + 配置解析
MqConfig parseMqConfig(const Json::Value& root);
std::shared_ptr<IMessageQueue> createMessageQueue(const Json::Value& root);

}  // namespace diag
