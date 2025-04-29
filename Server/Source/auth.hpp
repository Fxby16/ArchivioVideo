#pragma once

#include <string>
#include "session.hpp"

extern void td_auth_send_parameters(std::shared_ptr<ClientSession> session, const std::string &api_id, const std::string &api_hash, const std::string &directory);
extern void td_auth_send_number(std::shared_ptr<ClientSession> session, const std::string &phone);
extern void td_auth_send_code(std::shared_ptr<ClientSession> session, const std::string &code);
extern void td_auth_send_password(std::shared_ptr<ClientSession> session, const std::string &password);
extern std::string td_auth_get_state(std::shared_ptr<ClientSession> session);