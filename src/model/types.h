#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace diag {

// 诊断任务状态
enum class TaskStatus : uint8_t {
    PENDING = 0,   // 已创建，等待调度
    RUNNING = 1,   // 诊断执行中（ReAct 循环）
    SUCCEEDED = 2, // 诊断成功，已定位根因
    FAILED = 3,    // 诊断失败（LLM/工具不可恢复错误）
    TIMEOUT = 4,   // 超时（超出 max_steps 或 deadline）
    CANCELLED = 5, // 已取消
};

// 严重级别
enum class Severity : uint8_t {
    P0 = 0,
    P1 = 1,
    P2 = 2,
    P3 = 3,
};

// 诊断任务主记录
struct DiagnosisTask {
    uint64_t task_id{0};
    std::string service_name;   // 目标微服务
    std::string symptom;        // 故障现象/描述
    Severity severity{Severity::P2};
    TaskStatus status{TaskStatus::PENDING};
    int max_steps{10};          // 最大 ReAct 步数
    int current_step{0};        // 已执行步数
    std::string result;         // 最终结论/根因（SUCCEEDED 时为 JSON {answer,evidence,...}）
    std::string error_message;  // 失败原因
    double confidence{0.0};     // 置信分 0~1
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point updated_at{};
    std::optional<std::chrono::system_clock::time_point> started_at;   // NULL = 未开始
    std::optional<std::chrono::system_clock::time_point> finished_at;  // NULL = 未结束
    std::chrono::system_clock::time_point deadline{};   // 超时截止（UTC），0 = 未设置
};

// ReAct 单步轨迹（Thought/Action/Observation）
struct ReactStep {
    uint64_t step_id{0};
    uint64_t task_id{0};        // 外键 → diagnosis_task.id
    int step_no{0};
    std::string thought;        // 推理
    std::string action;         // 动作/工具名（如 "query_prometheus"）
    std::string action_input;   // 动作参数（JSON 字符串）
    std::string observation;    // 工具观察结果
    std::chrono::system_clock::time_point created_at{};
};

// 枚举 ↔ 字符串
const char* taskStatusToString(TaskStatus s);
const char* severityToString(Severity s);

// 枚举 ↔ 整数（用于落库 TINYINT）
uint8_t taskStatusToInt(TaskStatus s);
TaskStatus taskStatusFromInt(uint8_t v);
uint8_t severityToInt(Severity s);
Severity severityFromInt(uint8_t v);

}  // namespace diag
