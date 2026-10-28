#pragma once

#include <functional>
#include <memory>

#include <drogon/HttpController.h>

namespace diag {

class MetricsRegistry;

// 指标查询接口
class MetricsController : public drogon::HttpController<MetricsController, false> {
public:
    explicit MetricsController(std::shared_ptr<MetricsRegistry> registry);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MetricsController::getMetrics, "/api/metrics", drogon::Get);
    METHOD_LIST_END

    void getMetrics(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    std::shared_ptr<MetricsRegistry> registry_;
};

}  // namespace diag
