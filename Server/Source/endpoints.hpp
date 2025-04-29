#pragma once

#include <string>

extern void setup_endpoints();
extern int handle_video(struct mg_connection* conn, void* data);
extern int handle_get_files(struct mg_connection* conn, void* data);
extern int handle_auth(struct mg_connection* conn, void* data);
extern int handle_get_state(struct mg_connection* conn, void* data);
extern int handle_logout(struct mg_connection* conn, void* data);
extern int handle_chats(struct mg_connection* conn, void* data);

extern int get_videos_data_handler(struct mg_connection* conn, void* data); 
extern int set_video_data_handler(struct mg_connection* conn, void* data);