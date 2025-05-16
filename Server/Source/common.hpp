#pragma once

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

inline constexpr float TDLIB_TIMEOUT = 10.0f;

extern void td_send(const json& j, void* client);
extern json td_recv(void* client);
extern std::map<std::string, std::string> parse_query_string(const std::string& query_string);
extern std::string get_format_from_filename(const std::string& path);
extern std::string random_string(size_t length);
extern std::string format_unix_date(int64_t timestamp);