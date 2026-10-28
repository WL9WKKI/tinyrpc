#include <drogon/drogon.h>

int main() {
    // 加载运行时配置（监听端口、数据库、LLM 占位等）
    drogon::app().loadConfigFile("config.json");
    // 后续模块：注册 controller、初始化 mock 适配器、初始化 DbClient
    drogon::app().run();
    return 0;
}
