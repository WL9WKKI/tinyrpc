#include "controller/metrics_controller.h"

#include <drogon/HttpResponse.h>

#include "metrics/metrics.h"

namespace diag {

MetricsController::MetricsController(std::shared_ptr<MetricsRegistry> registry)
    : registry_(std::move(registry)) {}

void MetricsController::getMetrics(const drogon::HttpRequestPtr&,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto resp = drogon::HttpResponse::newHttpJsonResponse(registry_->report());
    callback(resp);
}

}  // namespace diag
