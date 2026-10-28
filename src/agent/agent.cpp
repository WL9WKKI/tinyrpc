#include "agent/agent.h"

#include <chrono>

#include <json/json.h>

#include "agent/cancel_manager.h"
#include "dao/task_store.h"
#include "executor/tool_executor.h"
#include "metrics/metrics.h"
#include "planner/planner.h"
#include "session/session_manager.h"

namespace diag {

namespace {
std::string stringifyJson(const Json::Value& v) {
    return Json::writeString(Json::StreamWriterBuilder(), v);
}
}  // namespace

ReActAgent::ReActAgent(std::shared_ptr<Planner> planner,
                       std::shared_ptr<ToolExecutor> executor,
                       std::shared_ptr<SessionManager> session,
                       std::shared_ptr<ITaskStore> store,
                       std::shared_ptr<CancelManager> canceller)
    : planner_(std::move(planner)), executor_(std::move(executor)),
      session_(std::move(session)), store_(std::move(store)),
      canceller_(std::move(canceller)) {}

AgentResult ReActAgent::run(const DiagnosisTask& task) {
    ScopedTimer t(metrics_.get(), "diagnosis");
    AgentResult result;
    auto flag = canceller_ ? canceller_->registerTask(task.task_id) : nullptr;
    auto cancelled = [&flag]() { return flag && flag->load(); };

    DiagnosisTask cur = task;  // 可变副本，边跑边更新

    // 启动即被取消
    if (cancelled()) {
        cur.status = TaskStatus::CANCELLED;
        store_->updateTask(cur);
        canceller_->unregister(task.task_id);
        result.status = TaskStatus::CANCELLED;
        result.error = "cancelled";
        return result;
    }

    session_->getOrCreate(task);

    // 置 RUNNING
    cur.status = TaskStatus::RUNNING;
    cur.started_at = std::chrono::system_clock::now();
    store_->updateTask(cur);

    for (int step = 0; step < task.max_steps; ++step) {
        if (cancelled()) {
            cur.status = TaskStatus::CANCELLED;
            cur.finished_at = std::chrono::system_clock::now();
            store_->updateTask(cur);
            canceller_->unregister(task.task_id);
            result.status = TaskStatus::CANCELLED;
            result.error = "cancelled";
            return result;
        }

        SessionContext ctx = session_->getOrCreate(task);
        auto messages = session_->buildMessages(ctx);
        PlanResult plan = planner_->plan(messages);

        if (plan.action == PlannerAction::FINISH) {
            // 聚合证据 + 置信打分
            auto trace = store_->getTrace(task.task_id);
            AggregateResult agg = aggregateEvidence(plan.answer, trace, agg_cfg_);
            cur.status = TaskStatus::SUCCEEDED;
            cur.result = stringifyJson(agg.toJson());
            cur.confidence = agg.confidence;
            cur.finished_at = std::chrono::system_clock::now();
            store_->updateTask(cur);
            canceller_->unregister(task.task_id);
            result.status = TaskStatus::SUCCEEDED;
            result.answer = plan.answer;
            return result;
        }
        if (plan.action == PlannerAction::ERROR) {
            cur.status = TaskStatus::FAILED;
            cur.error_message = plan.error;
            cur.finished_at = std::chrono::system_clock::now();
            store_->updateTask(cur);
            canceller_->unregister(task.task_id);
            result.status = TaskStatus::FAILED;
            result.error = plan.error;
            return result;
        }

        // 执行工具，组织 observation
        ToolCallResult exec = executor_->execute(plan.call.tool, plan.call.args);
        std::string observation;
        if (exec.status == ToolCallStatus::SUCCESS) {
            observation = stringifyJson(exec.data);
        } else {
            observation = "ERROR[" + std::string(toolCallStatusToString(exec.status)) +
                          "]: " + exec.error;
        }

        ReactStep stepRecord;
        stepRecord.task_id = task.task_id;
        stepRecord.step_no = step;
        stepRecord.action = plan.call.tool;
        stepRecord.action_input = stringifyJson(plan.call.args);
        stepRecord.observation = observation;
        store_->insertTrace(stepRecord);
        session_->appendStep(task.task_id, stepRecord);

        cur.current_step = step + 1;
        store_->updateTask(cur);
    }

    // 超出 max_steps
    cur.status = TaskStatus::TIMEOUT;
    cur.error_message = "exceeded max_steps";
    cur.finished_at = std::chrono::system_clock::now();
    store_->updateTask(cur);
    canceller_->unregister(task.task_id);
    result.status = TaskStatus::TIMEOUT;
    result.error = "exceeded max_steps";
    return result;
}

void ReActAgent::setAggregatorConfig(const AggregatorConfig& cfg) {
    agg_cfg_ = cfg;
}

void ReActAgent::setMetricsRegistry(std::shared_ptr<MetricsRegistry> metrics) {
    metrics_ = std::move(metrics);
}

}  // namespace diag
