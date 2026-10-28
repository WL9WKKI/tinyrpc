#pragma once

#include <memory>
#include <string>

#include "agent/aggregator.h"
#include "model/types.h"

namespace diag {

class Planner;
class ToolExecutor;
class SessionManager;
class ITaskStore;
class CancelManager;
class MetricsRegistry;

// 诊断结果
struct AgentResult {
    TaskStatus status{TaskStatus::FAILED};
    std::string answer;  // SUCCEEDED 时的结论
    std::string error;   // 失败原因
};

// 极简 ReAct 主循环：Planner → ToolExecutor → SessionManager，边跑边落库 + 支持取消
class ReActAgent {
public:
    ReActAgent(std::shared_ptr<Planner> planner,
               std::shared_ptr<ToolExecutor> executor,
               std::shared_ptr<SessionManager> session,
               std::shared_ptr<ITaskStore> store,
               std::shared_ptr<CancelManager> canceller);

    AgentResult run(const DiagnosisTask& task);

    void setAggregatorConfig(const AggregatorConfig& cfg);
    void setMetricsRegistry(std::shared_ptr<MetricsRegistry> metrics);

private:
    AgentResult runInternal(const DiagnosisTask& task);

    std::shared_ptr<Planner> planner_;
    std::shared_ptr<ToolExecutor> executor_;
    std::shared_ptr<SessionManager> session_;
    std::shared_ptr<ITaskStore> store_;
    std::shared_ptr<CancelManager> canceller_;
    AggregatorConfig agg_cfg_;
    std::shared_ptr<MetricsRegistry> metrics_;
};

}  // namespace diag
