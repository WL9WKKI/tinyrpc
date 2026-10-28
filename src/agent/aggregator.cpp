#include "agent/aggregator.h"

#include <cstdio>
#include <memory>

#include <json/json.h>

namespace diag {

const char* confidenceLevelToString(ConfidenceLevel l) {
    switch (l) {
        case ConfidenceLevel::LOW:    return "LOW";
        case ConfidenceLevel::MEDIUM: return "MEDIUM";
        case ConfidenceLevel::HIGH:   return "HIGH";
    }
    return "UNKNOWN";
}

Json::Value AggregateResult::toJson() const {
    Json::Value v;
    v["answer"] = answer;
    Json::Value ev(Json::arrayValue);
    for (const auto& e : evidence) ev.append(e);
    v["evidence"] = ev;
    v["confidence"] = confidence;
    v["level"] = confidenceLevelToString(level);
    return v;
}

namespace {

bool parseJson(const std::string& s, Json::Value& out) {
    Json::CharReaderBuilder b;
    const std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}

std::string fmtDouble(double v, int prec = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

// 指标信号：points 是否单调趋势（≥3 点连续涨/跌）
std::pair<double, std::string> extractMetric(const Json::Value& obs) {
    if (!obs.isMember("points") || !obs["points"].isArray() || obs["points"].size() < 3) {
        return {0.0, ""};
    }
    std::vector<double> vals;
    for (const auto& p : obs["points"]) {
        if (p.isMember("value")) vals.push_back(p["value"].asDouble());
    }
    if (vals.size() < 3) return {0.0, ""};
    bool inc = true, dec = true;
    for (size_t i = 1; i < vals.size(); ++i) {
        if (vals[i] <= vals[i - 1]) inc = false;
        if (vals[i] >= vals[i - 1]) dec = false;
    }
    bool trend = inc || dec;
    if (!trend) return {0.0, ""};
    std::string metric = obs.isMember("metric") ? obs["metric"].asString() : "metric";
    std::string ev = "[指标] " + metric + ": " + std::to_string(vals.size()) + " 个点 " +
                     std::to_string(static_cast<int>(vals.front())) + "→" +
                     std::to_string(static_cast<int>(vals.back())) +
                     (inc ? " 持续上涨" : " 持续下降");
    return {1.0, ev};
}

// 日志信号：是否有 ERROR/FATAL 级或 OOM/Exception 关键字
std::pair<double, std::string> extractLog(const Json::Value& obs) {
    if (!obs.isMember("logs") || !obs["logs"].isArray()) return {0.0, ""};
    int errorCount = 0;
    bool hasOom = false;
    for (const auto& l : obs["logs"]) {
        std::string level = l.isMember("level") ? l["level"].asString() : "";
        std::string msg = l.isMember("message") ? l["message"].asString() : "";
        if (level == "ERROR" || level == "FATAL") errorCount++;
        if (msg.find("OOM") != std::string::npos || msg.find("OutOfMemory") != std::string::npos ||
            msg.find("Exception") != std::string::npos) {
            hasOom = true;
        }
    }
    bool anomaly = errorCount > 0 || hasOom;
    if (!anomaly) return {0.0, ""};
    std::string service = obs.isMember("service") ? obs["service"].asString() : "service";
    std::string ev = "[日志] " + service + ": " + std::to_string(errorCount) + " 条 ERROR";
    if (hasOom) ev += "（含 OOM/Exception）";
    return {1.0, ev};
}

// 案例信号：top1 案例相似度
std::pair<double, std::string> extractCase(const Json::Value& obs) {
    if (!obs.isMember("cases") || !obs["cases"].isArray() || obs["cases"].size() == 0) {
        return {0.0, ""};
    }
    const Json::Value& c = obs["cases"][0];
    double sim = c.isMember("similarity") ? c["similarity"].asDouble() : 0.0;
    if (sim <= 0) return {0.0, ""};
    std::string title = c.isMember("title") ? c["title"].asString() : "";
    std::string root = c.isMember("root_cause") ? c["root_cause"].asString() : "";
    std::string ev = "[案例] " + title + " 相似度 " + fmtDouble(sim);
    if (!root.empty()) ev += "，根因：" + root;
    return {sim, ev};
}

}  // namespace

AggregateResult aggregateEvidence(const std::string& answer,
                                  const std::vector<ReactStep>& trace,
                                  const AggregatorConfig& cfg) {
    AggregateResult res;
    res.answer = answer;

    // 1. 按工具提取信号 + 证据
    std::map<std::string, double> signals;  // tool -> signal
    for (const auto& step : trace) {
        Json::Value obs;
        if (!parseJson(step.observation, obs)) continue;

        std::pair<double, std::string> sig;
        if (step.action == "query_metric") sig = extractMetric(obs);
        else if (step.action == "query_log") sig = extractLog(obs);
        else if (step.action == "search_case") sig = extractCase(obs);
        else continue;  // 未知工具跳过

        if (!sig.second.empty()) res.evidence.push_back(sig.second);
        signals[step.action] = sig.first;  // 同工具多次取最后一次
    }

    // 2. 归一化权重 → 加权求和（未调用的工具 signal=0，拉低置信分）
    double weightSum = 0.0;
    for (const auto& kv : cfg.weights) weightSum += kv.second;
    if (weightSum <= 0.0) {
        res.confidence = 0.0;
    } else {
        double conf = 0.0;
        for (const auto& kv : cfg.weights) {
            auto it = signals.find(kv.first);
            double s = (it != signals.end()) ? it->second : 0.0;
            conf += (kv.second / weightSum) * s;
        }
        res.confidence = conf > 1.0 ? 1.0 : conf;
    }

    // 3. 定等级
    if (res.confidence >= cfg.high_threshold) res.level = ConfidenceLevel::HIGH;
    else if (res.confidence >= cfg.low_threshold) res.level = ConfidenceLevel::MEDIUM;
    else res.level = ConfidenceLevel::LOW;

    return res;
}

AggregatorConfig parseAggregatorConfig(const Json::Value& root) {
    AggregatorConfig cfg;
    if (!root.isMember("aggregator") || !root["aggregator"].isObject()) return cfg;
    const Json::Value& a = root["aggregator"];
    if (a.isMember("weights") && a["weights"].isObject()) {
        const Json::Value& w = a["weights"];
        for (const auto& key : w.getMemberNames()) {
            cfg.weights[key] = w[key].asDouble();
        }
    }
    if (a.isMember("thresholds") && a["thresholds"].isObject()) {
        const Json::Value& t = a["thresholds"];
        if (t.isMember("low")) cfg.low_threshold = t["low"].asDouble();
        if (t.isMember("high")) cfg.high_threshold = t["high"].asDouble();
    }
    return cfg;
}

}  // namespace diag
