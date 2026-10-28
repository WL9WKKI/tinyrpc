#include "types.h"

namespace diag {

const char* taskStatusToString(TaskStatus s) {
    switch (s) {
        case TaskStatus::PENDING:   return "PENDING";
        case TaskStatus::RUNNING:   return "RUNNING";
        case TaskStatus::SUCCEEDED: return "SUCCEEDED";
        case TaskStatus::FAILED:    return "FAILED";
        case TaskStatus::TIMEOUT:   return "TIMEOUT";
        case TaskStatus::CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

const char* severityToString(Severity s) {
    switch (s) {
        case Severity::P0: return "P0";
        case Severity::P1: return "P1";
        case Severity::P2: return "P2";
        case Severity::P3: return "P3";
    }
    return "UNKNOWN";
}

uint8_t taskStatusToInt(TaskStatus s) {
    return static_cast<uint8_t>(s);
}

TaskStatus taskStatusFromInt(uint8_t v) {
    switch (static_cast<TaskStatus>(v)) {
        case TaskStatus::PENDING:
        case TaskStatus::RUNNING:
        case TaskStatus::SUCCEEDED:
        case TaskStatus::FAILED:
        case TaskStatus::TIMEOUT:
        case TaskStatus::CANCELLED:
            return static_cast<TaskStatus>(v);
    }
    return TaskStatus::PENDING;  // 非法值兜底
}

uint8_t severityToInt(Severity s) {
    return static_cast<uint8_t>(s);
}

Severity severityFromInt(uint8_t v) {
    switch (static_cast<Severity>(v)) {
        case Severity::P0:
        case Severity::P1:
        case Severity::P2:
        case Severity::P3:
            return static_cast<Severity>(v);
    }
    return Severity::P2;  // 非法值兜底
}

}  // namespace diag
