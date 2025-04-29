#pragma once

#include <nlohmann/json.hpp>
#include <map>
#include <string>

using json = nlohmann::json;

inline constexpr float TDLIB_TIMEOUT = 10.0f;

extern void td_send(const json& j, void* client);
extern json td_recv(void* client);
extern std::map<std::string, std::string> parse_query_string(const std::string& query_string);