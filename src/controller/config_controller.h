#pragma once

#include <functional>
#include <memory>
#include <string>

#include <drogon/HttpController.h>

namespace diag {

class ReActAgent;

// 配置管理接口（读/写 config.json + 权重热加载）
class ConfigController : public drogon::HttpController<ConfigController, false> {
public:
    ConfigController(std::shared_ptr<ReActAgent> agent, std::string configPath);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigController::getConfig, "/api/config", drogon::Get);
    ADD_METHOD_TO(ConfigController::putConfig, "/api/config", drogon::Put);
    METHOD_LIST_END

    void getConfig(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void putConfig(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    std::shared_ptr<ReActAgent> agent_;
    std::string config_path_;
};

}  // namespace diag
