#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <json/json.h>

#include "model/types.h"

namespace diag {

// 置信等级
enum class ConfidenceLevel : uint8_t {
    LOW = 0,    // < low_threshold
    MEDIUM = 1, // low ~ high
    HIGH = 2,   // >= high_threshold
};

// 聚合结果
struct AggregateResult {
    std::string answer;                  // LLM 结论
    std::vector<std::string> evidence;   // 证据条目（可读文本）
    double confidence{0.0};              // 置信分 0~1
    ConfidenceLevel level{ConfidenceLevel::LOW};

    Json::Value toJson() const;  // {answer, evidence, confidence, level}
};

// 工具名 -> 权重
using WeightMap = std::map<std::string, double>;

// 聚合配置（权重 + 等级阈值）
struct AggregatorConfig {
    WeightMap weights;
    double low_threshold{0.4};
    double high_threshold{0.7};
};

// 从诊断轨迹提取证据信号 + 计算置信分
AggregateResult aggregateEvidence(const std::string& answer,
                                  const std::vector<ReactStep>& trace,
                                  const AggregatorConfig& cfg);

const char* confidenceLevelToString(ConfidenceLevel l);

// 从 config 解析（aggregator 段）
AggregatorConfig parseAggregatorConfig(const Json::Value& root);

}  // namespace diag
