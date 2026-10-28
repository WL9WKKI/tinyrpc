#include "tools/base_tool.h"

namespace diag {

const char* toolTypeToString(ToolType t) {
    switch (t) {
        case ToolType::METRIC: return "metric";
        case ToolType::LOG:    return "log";
        case ToolType::CASE:   return "case";
    }
    return "unknown";
}

const char* toolStatusToString(ToolStatus s) {
    switch (s) {
        case ToolStatus::SUCCESS: return "SUCCESS";
        case ToolStatus::FAILED:  return "FAILED";
        case ToolStatus::TIMEOUT: return "TIMEOUT";
    }
    return "UNKNOWN";
}

}  // namespace diag
