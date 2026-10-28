#include "metrics/metrics.h"

#include <hdr/hdr_histogram.h>

namespace diag {

MetricsRegistry::MetricsRegistry() = default;

MetricsRegistry::~MetricsRegistry() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : histograms_) {
        hdr_close(kv.second);
    }
    histograms_.clear();
}

struct hdr_histogram* MetricsRegistry::getOrCreate(const std::string& metric) {
    auto it = histograms_.find(metric);
    if (it != histograms_.end()) return it->second;
    struct hdr_histogram* h = nullptr;
    // 1 微秒 ~ 1 小时，3 位有效数字
    if (hdr_init(1, 3600000000LL, 3, &h) != 0) return nullptr;
    histograms_[metric] = h;
    return h;
}

void MetricsRegistry::record(const std::string& metric, int64_t micros) {
    std::lock_guard<std::mutex> lock(mutex_);
    struct hdr_histogram* h = getOrCreate(metric);
    if (h) hdr_record_value(h, micros);
}

Json::Value MetricsRegistry::report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json::Value out(Json::objectValue);
    for (const auto& kv : histograms_) {
        const struct hdr_histogram* h = kv.second;
        Json::Value m;
        m["count"] = static_cast<Json::Int64>(hdr_total_count(h));
        m["p50_ms"] = hdr_value_at_percentile(h, 50.0) / 1000.0;
        m["p95_ms"] = hdr_value_at_percentile(h, 95.0) / 1000.0;
        m["p99_ms"] = hdr_value_at_percentile(h, 99.0) / 1000.0;
        m["mean_ms"] = hdr_mean(h) / 1000.0;
        out[kv.first] = m;
    }
    return out;
}

// ---- ScopedTimer ----

ScopedTimer::ScopedTimer(MetricsRegistry* reg, const std::string& metric)
    : reg_(reg), metric_(metric), start_(std::chrono::steady_clock::now()) {}

ScopedTimer::~ScopedTimer() {
    if (!reg_) return;
    auto end = std::chrono::steady_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    reg_->record(metric_, micros);
}

}  // namespace diag
