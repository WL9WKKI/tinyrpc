#pragma once

#include "model/types.h"

namespace diag {

// 任务状态机：校验状态流转是否合法
class TaskStateMachine {
public:
    // 判断 from → to 是否合法
    static bool canTransition(TaskStatus from, TaskStatus to);

    // 是否为终止态（不可再流转）
    static bool isTerminal(TaskStatus s);
};

}  // namespace diag
