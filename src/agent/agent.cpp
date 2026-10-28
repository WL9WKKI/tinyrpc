#include "agent/agent.h"

#include <json/json.h>

#include "executor/tool_executor.h"
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
                       std::shared_ptr<SessionManager> session)
    : planner_(std::move(planner)), executor_(std::move(executor)), session_(std::move(session)) {}

AgentResult ReActAgent::run(const DiagnosisTask& task) {
    AgentResult result;
    // 初始化会话（system + 现象），后续每次取最新上下文
    session_->getOrCreate(task);

    for (int step = 0; step < task.max_steps; ++step) {
        SessionContext ctx = session_->getOrCreate(task);
        auto messages = session_->buildMessages(ctx);

        PlanResult plan = planner_->plan(messages);

        if (plan.action == PlannerAction::FINISH) {
            result.status = TaskStatus::SUCCEEDED;
            result.answer = plan.answer;
            return result;
        }
        if (plan.action == PlannerAction::ERROR) {
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
        stepRecord.step_no = step;
        stepRecord.action = plan.call.tool;
        stepRecord.action_input = stringifyJson(plan.call.args);
        stepRecord.observation = observation;
        // thought 暂空：Planner 目前不单独返回 thought
        session_->appendStep(task.task_id, stepRecord);
    }

    result.status = TaskStatus::TIMEOUT;
    result.error = "exceeded max_steps";
    return result;
}

// ---- 工厂 ----

std::shared_ptr<ReActAgent> createAgent(const Json::Value& root) {
    auto session = createSessionManager(root);
    auto executor = createToolExecutor(root);
    auto planner = createPlanner(root, executor->describeTools());
    return std::make_shared<ReActAgent>(planner, executor, session);
}

}  // namespace diag
