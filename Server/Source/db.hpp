#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

extern int connect_db();
extern int disconnect_db();
extern json db_select(const std::string& query);
extern int db_execute(const std::string& query);
extern std::string escape_string(const std::string& str);
