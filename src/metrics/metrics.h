#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <json/json.h>

struct hdr_histogram;  // HdrHistogram_c 前置声明

namespace diag {

// 指标注册表：一组命名直方图，线程安全
class MetricsRegistry {
public:
    MetricsRegistry();
    ~MetricsRegistry();

    // 记录一次耗时（微秒）
    void record(const std::string& metric, int64_t micros);

    // 报告所有指标（count / p50 / p95 / p99 / mean，毫秒）
    Json::Value report() const;

private:
    struct hdr_histogram* getOrCreate(const std::string& metric);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, struct hdr_histogram*> histograms_;
};

// RAII 计时器：构造开始，析构记录（reg 为空则 no-op）
class ScopedTimer {
public:
    ScopedTimer(MetricsRegistry* reg, const std::string& metric);
    ~ScopedTimer();

private:
    MetricsRegistry* reg_;
    std::string metric_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace diag
