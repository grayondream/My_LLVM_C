#include <spdlog/spdlog.h>

#define LOGI(fmt, ...) spdlog::info(fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) spdlog::debug(fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) spdlog::error(fmt, ##__VA_ARGS__)