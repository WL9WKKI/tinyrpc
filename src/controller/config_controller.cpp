#include "controller/config_controller.h"

#include <fstream>
#include <iostream>
#include <memory>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "agent/agent.h"
#include "agent/aggregator.h"

namespace diag {

namespace {
// 业务配置段（不含 Drogon 自身的 listeners/app/db_clients）
const char* kBusinessKeys[] = {"agent", "tools", "aggregator", "redis", "session", "mq", "storage", "executor"};

Json::Value loadConfigFile(const std::string& path) {
    Json::Value root;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return root;
    Json::CharReaderBuilder b;
    std::string errs;
    Json::parseFromStream(b, ifs, &root, &errs);
    return root;
}

bool writeConfigFile(const std::string& path, const Json::Value& root) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";  // 2 空格缩进
    builder["precision"] = 10;      // 压缩浮点数（0.4 而非 0.40000000000000002）
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << Json::writeString(builder, root);
    return true;
}

drogon::HttpResponsePtr jsonResponse(const Json::Value& v,
                                     drogon::HttpStatusCode code = drogon::k200OK) {
    auto resp = drogon::HttpResponse::newHttpJsonResponse(v);
    resp->setStatusCode(code);
    return resp;
}
}  // namespace

ConfigController::ConfigController(std::shared_ptr<ReActAgent> agent, std::string configPath)
    : agent_(std::move(agent)), config_path_(std::move(configPath)) {}

void ConfigController::getConfig(const drogon::HttpRequestPtr&,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value full = loadConfigFile(config_path_);
    Json::Value business;
    for (const char* k : kBusinessKeys) {
        if (full.isMember(k)) business[k] = full[k];
    }
    callback(jsonResponse(business));
}

void ConfigController::putConfig(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto jsonPtr = req->getJsonObject();
    if (!jsonPtr) {
        Json::Value e;
        e["error"] = "invalid JSON body";
        callback(jsonResponse(e, drogon::k400BadRequest));
        return;
    }
    const Json::Value& body = *jsonPtr;

    Json::Value full = loadConfigFile(config_path_);
    Json::Value hotReloaded(Json::arrayValue);
    Json::Value restartRequired(Json::arrayValue);
    bool hasAggregator = false;

    for (const char* k : kBusinessKeys) {
        if (!body.isMember(k)) continue;
        full[k] = body[k];
        if (std::string(k) == "aggregator") {
            hotReloaded.append(k);
            hasAggregator = true;
        } else {
            restartRequired.append(k);
        }
    }

    if (!writeConfigFile(config_path_, full)) {
        Json::Value e;
        e["error"] = "写入 config.json 失败";
        callback(jsonResponse(e, drogon::k500InternalServerError));
        return;
    }

    // aggregator 权重热加载（即时生效）
    if (hasAggregator && agent_) {
        agent_->setAggregatorConfig(parseAggregatorConfig(full));
    }

    Json::Value resp;
    resp["ok"] = true;
    resp["hot_reloaded"] = hotReloaded;
    resp["restart_required"] = restartRequired;
    callback(jsonResponse(resp));
}

}  // namespace diag
