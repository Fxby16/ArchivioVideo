#pragma once

#include "common.hpp"
#include "session.hpp"

extern std::vector<json> get_chats(std::shared_ptr<ClientSession> session);
extern std::vector<json> get_videos_from_channel(std::shared_ptr<ClientSession> session, const std::string &chat_id, int64_t from_message_id, int limit);