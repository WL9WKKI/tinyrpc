#pragma once

#include <string>
#include <utility>

#include <json/json.h>

#include "tools/base_tool.h"

namespace diag {

// Prometheus 后端配置
struct PrometheusConfig {
    std::string base_url{"http://127.0.0.1:9090"};
    int         timeout_ms{5000};
    int         retries{2};  // HTTP 瞬时失败重试次数
};

// Elasticsearch 后端配置
struct ElasticsearchConfig {
    std::string base_url{"http://127.0.0.1:9200"};
    std::string index{"logs"};   // 默认日志索引
    int         timeout_ms{5000};
    int         retries{2};  // HTTP 瞬时失败重试次数
};

PrometheusConfig    parsePrometheusConfig(const Json::Value& metricCfg);
ElasticsearchConfig parseEsConfig(const Json::Value& logCfg);

// 指标查询（Prometheus）—— 真实
class RealMetricTool : public BaseTool {
public:
    explicit RealMetricTool(PrometheusConfig cfg) : cfg_(std::move(cfg)) {}

    std::string name() const override;
    ToolType type() const override;
    std::string description() const override;
    Json::Value inputSchema() const override;
    ToolResult execute(const Json::Value& args) override;

private:
    PrometheusConfig cfg_;
};

// 日志查询（Elasticsearch）—— 真实
class RealLogTool : public BaseTool {
public:
    explicit RealLogTool(ElasticsearchConfig cfg) : cfg_(std::move(cfg)) {}

    std::string name() const override;
    ToolType type() const override;
    std::string description() const override;
    Json::Value inputSchema() const override;
    ToolResult execute(const Json::Value& args) override;

private:
    ElasticsearchConfig cfg_;
};

}  // namespace diag
