#include "state_machine.h"

namespace diag {

bool TaskStateMachine::isTerminal(TaskStatus s) {
    switch (s) {
        case TaskStatus::SUCCEEDED:
        case TaskStatus::FAILED:
        case TaskStatus::TIMEOUT:
        case TaskStatus::CANCELLED:
            return true;
        default:
            return false;
    }
}

bool TaskStateMachine::canTransition(TaskStatus from, TaskStatus to) {
    // 终止态不可再流转
    if (isTerminal(from)) {
        return false;
    }
    switch (from) {
        case TaskStatus::PENDING:
            return to == TaskStatus::RUNNING ||
                   to == TaskStatus::FAILED ||
                   to == TaskStatus::TIMEOUT ||
                   to == TaskStatus::CANCELLED;
        case TaskStatus::RUNNING:
            return to == TaskStatus::SUCCEEDED ||
                   to == TaskStatus::FAILED ||
                   to == TaskStatus::TIMEOUT ||
                   to == TaskStatus::CANCELLED;
        default:
            return false;
    }
}

}  // namespace diag
