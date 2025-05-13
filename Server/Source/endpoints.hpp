#pragma once

#include <string>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

extern void setup_endpoints();
extern int handle_video(const httplib::Request&, httplib::Response&);
extern int handle_image(const httplib::Request&, httplib::Response&);
extern int handle_get_files(const httplib::Request&, httplib::Response&);
extern int handle_auth(const httplib::Request&, httplib::Response&);
extern int handle_get_state(const httplib::Request&, httplib::Response&);
extern int handle_logout(const httplib::Request&, httplib::Response&);
extern int handle_chats(const httplib::Request&, httplib::Response&);
extern int handle_upload(const httplib::Request&, httplib::Response&);

extern int get_videos_data_handler(const httplib::Request&, httplib::Response&); 
extern int set_video_data_handler(const httplib::Request&, httplib::Response&);