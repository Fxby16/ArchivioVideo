#include "endpoints.hpp"
#include "auth.hpp"
#include "common.hpp"
#include "chats.hpp"
#include "app_data.hpp"
#include "session.hpp"
#include "random.hpp"
#include "db.hpp"

#include <civetweb.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <fstream>

void setup_endpoints()
{
    const char* options[] = {
        "listening_ports", "10000",
        nullptr 
    };

    mg_callbacks callbacks = {};
    mg_context *ctx = mg_start(&callbacks, nullptr, options);

    //telegram
    mg_set_request_handler(ctx, "/get_files", handle_get_files, nullptr);
    mg_set_request_handler(ctx, "/video", handle_video, nullptr);
    mg_set_request_handler(ctx, "/auth", handle_auth, nullptr);
    mg_set_request_handler(ctx, "/auth/get_state", handle_get_state, nullptr);
    mg_set_request_handler(ctx, "/logout", handle_logout, nullptr);
    mg_set_request_handler(ctx, "/get_chats", handle_chats, nullptr);
    mg_set_request_handler(ctx, "/upload", handle_upload, nullptr);

    //db
    mg_set_request_handler(ctx, "/set_video_data", set_video_data_handler, nullptr);
    mg_set_request_handler(ctx, "/get_videos_data", get_videos_data_handler, nullptr);
}

int handle_chats(struct mg_connection* conn, void*)
{
    const mg_request_info* req_info = mg_get_request_info(conn);

    // Parse query string
    std::string query_string = req_info->query_string ? req_info->query_string : "";
    std::map<std::string, std::string> params = parse_query_string(query_string);

    uint32_t session_id = 0;

    // Extract the session_id parameter
    if(params.find("session_id") != params.end()){
        session_id = std::stoul(params["session_id"]);
    }else{
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 400;
    }

    std::vector<json> chats = get_chats(getSession(session_id));
    std::string json_str = json(chats).dump();

    mg_printf(conn, "HTTP/1.1 200 OK\r\n");
    mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
    mg_printf(conn, "Content-Type: application/json\r\n");
    mg_printf(conn, "Content-Length: %zu\r\n", json_str.size());
    mg_printf(conn, "\r\n");
    mg_write(conn, json_str.c_str(), json_str.size());
    return 200;
}

int handle_video(struct mg_connection* conn, void *)
{
    const mg_request_info* req_info = mg_get_request_info(conn);

    const char* range_header = mg_get_header(conn, "Range");
    
    if(range_header){
        size_t start, end;
        unsigned int file_id = 0;
        uint32_t session_id = 0;

        // Parse the query string to extract parameters
        std::string query_string = req_info->query_string ? req_info->query_string : "";
        std::map<std::string, std::string> params = parse_query_string(query_string);

        // Extract the file_id parameter
        if(params.find("file_id") != params.end()){
            file_id = std::stoul(params["file_id"]);
        }else{
            std::cerr << "[ERROR] Missing file_id parameter in request." << std::endl;
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 400;
        }

        // Extract the session_id parameter
        if(params.find("session_id") != params.end()){
            session_id = std::stoul(params["session_id"]);
        }else{
            std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 400;
        }

        // Extract the range values
        int matched = sscanf(range_header, "bytes=%zu-%zu", &start, &end);

        if(matched == 1){ // If only start is provided
            end = start + 1024 * 1024 - 1; // Default to 1 MB range
        }else if(matched != 2){ // If the format is incorrect
            std::cerr << "❌ Invalid Range header format" << std::endl;
            std::cerr << "📥 Range Header: " << range_header << std::endl;

            mg_printf(conn, "HTTP/1.1 416 Range Not Satisfiable\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 416;
        }

        std::shared_ptr<ClientSession> session = getSession(session_id);

        // Request chunk from TDLib
        session->send({{"@type", "downloadFile"},
                    {"file_id", file_id},
                    {"priority", 1},
                    {"offset", start},
                    {"limit", end - start + 1},
                    {"synchronous", false}});

        //{{file_id, session_id}, response_id}
        static std::map<std::pair<unsigned int, uint32_t>,uint32_t> last_checked_map;

        std::pair<unsigned int, uint32_t> key = std::make_pair(file_id, session_id);

        while(true){
            // Get new responses
            auto responses = session->getResponses()->get_all(last_checked_map[key]);

            for(auto r = responses.begin(); r != responses.end(); r++){
                last_checked_map[key] = r->first; // Save last checked

                json response = r->second;
                if(!response.is_null() && response["@type"] == "updateFile"){
                    // If it's the requested chunk and it has been downloaded
                    if(response["file"]["id"] == file_id && response["file"]["local"]["is_downloading_active"] == false){
                        size_t available_start = response["file"]["local"]["download_offset"];
                        size_t available_size = response["file"]["local"]["downloaded_size"];
                        size_t available_end = available_start + available_size - 1;

                        // Handle file smaller than the requested range
                        end = std::min(std::min(end, (size_t) response["file"]["expected_size"] - 1), available_end);

                        std::string file_path = response["file"]["local"]["path"];
                        int file_size = response["file"]["expected_size"];
                        std::ifstream file(file_path, std::ios::binary);

                        if(file){
                            size_t length = end - start + 1;
                            mg_printf(conn, "HTTP/1.1 206 Partial Content\r\n");
                            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                            mg_printf(conn, "Content-Type: video/mp4\r\n");
                            mg_printf(conn, "Accept-Ranges: bytes\r\n");
                            mg_printf(conn, "Content-Length: %zu\r\n", length);
                            mg_printf(conn, "Content-Range: bytes %zu-%zu/%d\r\n", start, end, file_size);
                            mg_printf(conn, "\r\n");

                            file.seekg(start);
                            std::vector<char> buffer(length);
                            file.read(buffer.data(), length);
                            mg_write(conn, buffer.data(), length);
                            file.close();

                            return 200;
                        }else{
                            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
                            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n");
                            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                            mg_printf(conn, "\r\n");
                            return 500;
                        }
                    }
                }
            }
        }
    }else{
        std::cerr << "[ERROR] Range header not found" << std::endl;
        mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 405;
    }

    return 200;
}

int handle_get_files(struct mg_connection* conn, void* data)
{
    const mg_request_info *req_info = mg_get_request_info(conn);

    // Parse query string
    std::string query_string = req_info->query_string ? req_info->query_string : "";
    std::map<std::string, std::string> params = parse_query_string(query_string);

    uint32_t session_id = 0;
    std::string chat_id;

    // Extract the session_id parameter
    if(params.find("session_id") != params.end()){
        session_id = std::stoul(params["session_id"]);
    }else{
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 400;
    }

    // Extract the chat_id parameter
    if(params.find("chat_id") != params.end()){
        chat_id = params["chat_id"];
    }else{
        std::cerr << "[ERROR] Missing chat_id parameter in request." << std::endl;
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 400;
    }

    std::shared_ptr<ClientSession> session = getSession(session_id);
    std::vector<json> videos = get_videos_from_channel(session, chat_id, 0, 100);

    json files_json;
    for(const auto &video : videos){
        files_json.push_back(video);
    }

    std::string json_str = files_json.dump();

    mg_printf(conn, "HTTP/1.1 200 OK\r\n");
    mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
    mg_printf(conn, "Content-Type: application/json\r\n");
    mg_printf(conn, "Content-Length: %zu\r\n", json_str.size());
    mg_printf(conn, "\r\n");

    mg_write(conn, json_str.c_str(), json_str.size());

    return 200;
}

int handle_auth(struct mg_connection* conn, void* data)
{
    const mg_request_info* req_info = mg_get_request_info(conn);

    if(req_info->request_method && std::string(req_info->request_method) == "POST"){
        char post_data[1024];
        int post_data_len = mg_read(conn, post_data, sizeof(post_data) - 1);

        if(post_data_len > 0){
            post_data[post_data_len] = '\0'; // Null-terminate the data

            json request_json = json::parse(post_data);

            if(request_json.contains("start_auth")){ // Initiate authentication process
                uint32_t session_id = 0;

                if(request_json.contains("session_id") && request_json["session_id"] != 0){ // Client already logged in
                    session_id = std::stoul(request_json["session_id"].get<std::string>());
                }else{ // New client, generate session id
                    session_id = rand_uint32();
                }

                std::shared_ptr<ClientSession> session = getSession(session_id);
                std::string directory = std::to_string(session_id);

                session->send({{"@type", "getAuthorizationState"}});
                td_auth_send_parameters(session, APP_API_ID, APP_API_HASH, directory);

                mg_printf(conn, "HTTP/1.1 200 OK\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "Content-Type: application/json\r\n");
                mg_printf(conn, "\r\n");

                json response_json = {{"status", "success"},
                                      {"session_id", session_id}}; // Send session id to client for future requests

                std::string response_str = response_json.dump();
                mg_write(conn, response_str.c_str(), response_str.size());

                return 200;
            }

            // To continue the authentication, the client must have a session open
            if(!request_json.contains("session_id")){
                std::cerr << "[ERROR] Missing session_id in JSON." << std::endl;
                mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "\r\n");
                return 400;
            }

            uint32_t session_id = std::stoul(request_json["session_id"].get<std::string>());
            std::shared_ptr<ClientSession> session = getSession(session_id);
            
            if(request_json.contains("phone_number")){
                std::string phone_number = request_json["phone_number"];

                td_auth_send_number(session, phone_number);

                mg_printf(conn, "HTTP/1.1 200 OK\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "Content-Type: application/json\r\n");
                mg_printf(conn, "\r\n");

                json response_json = {{"status", "success"}};
                std::string response_str = response_json.dump();
                mg_write(conn, response_str.c_str(), response_str.size());

                return 200;
            }else if(request_json.contains("code")){
                std::string code = request_json["code"];

                td_auth_send_code(session, code);

                mg_printf(conn, "HTTP/1.1 200 OK\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "Content-Type: application/json\r\n");
                mg_printf(conn, "\r\n");

                json response_json = {{"status", "success"}};
                std::string response_str = response_json.dump();
                mg_write(conn, response_str.c_str(), response_str.size());

                return 200;
            }else if(request_json.contains("password")){
                std::string password = request_json["password"];

                td_auth_send_password(session, password);

                mg_printf(conn, "HTTP/1.1 200 OK\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "Content-Type: application/json\r\n");
                mg_printf(conn, "\r\n");

                json response_json = {{"status", "success"}};
                std::string response_str = response_json.dump();
                mg_write(conn, response_str.c_str(), response_str.size());

                return 200;
            }else{
                std::cerr << "[ERROR] Missing required fields in JSON." << std::endl;
                mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "\r\n");
                return 400;
            }
        }else{
            std::cerr << "[ERROR] No POST data received." << std::endl;
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 400;
        }
    }else{
        std::cerr << "[ERROR] Unsupported request method: " << (req_info->request_method ? req_info->request_method : "null") << std::endl;
        mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 405;
    }
}

int handle_get_state(struct mg_connection* conn, void* data)
{
    const mg_request_info* req_info = mg_get_request_info(conn);

    // Parse query string
    std::string query_string = req_info->query_string ? req_info->query_string : "";
    std::map<std::string, std::string> params = parse_query_string(query_string);

    uint32_t session_id = 0;

    // Extract the session_id parameter
    if(params.find("session_id") != params.end()){
        session_id = std::stoul(params["session_id"]);
    }else{
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 400;
    }

    std::shared_ptr<ClientSession> session = getSession(session_id);

    session->send({{"@type", "getAuthorizationState"}});
    json state_json = {{"state", td_auth_get_state(session)}};

    std::string json_str = state_json.dump();

    mg_printf(conn, "HTTP/1.1 200 OK\r\n");
    mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
    mg_printf(conn, "Content-Type: application/json\r\n");
    mg_printf(conn, "Content-Length: %zu\r\n", json_str.size());
    mg_printf(conn, "\r\n");
    mg_write(conn, json_str.c_str(), json_str.size());

    return 200;
}

int handle_logout(struct mg_connection *conn, void *data)
{
    const mg_request_info* req_info = mg_get_request_info(conn);

    // Parse query string
    std::string query_string = req_info->query_string ? req_info->query_string : "";
    std::map<std::string, std::string> params = parse_query_string(query_string);

    uint32_t session_id = 0;

    if(params.find("session_id") != params.end()){
        session_id = std::stoul(params["session_id"]);
    }else{
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 400;
    }

    std::shared_ptr<ClientSession> session = getSession(session_id);

    session->send({{"@type", "logOut"}});
    
    std::cout << "[MESSAGE] Logging out..." << std::endl;

    while(td_auth_get_state(session) != "authorizationStateClosed"){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << "[MESSAGE] Logged out successfully!" << std::endl;

    session->send({{"@type", "close"}});

    closeSession(session_id);

    std::cout << "[MESSAGE] Session closed!" << std::endl;

    std::filesystem::path session_dir = std::filesystem::path("UserData") / std::to_string(session_id);

    if(std::filesystem::exists(session_dir)){
        std::filesystem::remove_all(session_dir);
    }

    mg_printf(conn, "HTTP/1.1 200 OK\r\n");
    mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
    mg_printf(conn, "Content-Type: application/json\r\n");
    mg_printf(conn, "\r\n");
    json response_json = {{"status", "success"}};
    std::string response_str = response_json.dump();
    mg_write(conn, response_str.c_str(), response_str.size());

    return 200;
}

//DA SALVARE NEL DB (ANCORA DA TESTARE)
int set_video_data_handler(struct mg_connection* conn, void* data)
{
    const mg_request_info* req_info = mg_get_request_info(conn);

    if(req_info->request_method && std::string(req_info->request_method) == "POST"){
        char content_type[128];
        const char* content_type_header = mg_get_header(conn, "Content-Type");

        if(content_type_header && sscanf(content_type_header, "multipart/form-data; boundary=%127s", content_type) == 1){
            std::string boundary = "--" + std::string(content_type);
            char buffer[4096];
            int bytes_read;
            std::string current_part;
            std::string title, description, uploader, image_data;

            while((bytes_read = mg_read(conn, buffer, sizeof(buffer))) > 0){
                current_part.append(buffer, bytes_read);

                // Process each part
                size_t boundary_pos;
                while((boundary_pos = current_part.find(boundary)) != std::string::npos){
                    std::string part = current_part.substr(0, boundary_pos);
                    current_part.erase(0, boundary_pos + boundary.size());

                    // Parse headers and content
                    size_t header_end = part.find("\r\n\r\n");
                    if (header_end != std::string::npos) {
                        std::string headers = part.substr(0, header_end);
                        std::string content = part.substr(header_end + 4);

                        if(headers.find("Content-Disposition: form-data; name=\"title\"") != std::string::npos){
                            title = content;
                        }else if(headers.find("Content-Disposition: form-data; name=\"description\"") != std::string::npos){
                            description = content;
                        }else if(headers.find("Content-Disposition: form-data; name=\"uploader\"") != std::string::npos){
                            uploader = content;
                        }else if(headers.find("Content-Disposition: form-data; name=\"image\"") != std::string::npos){
                            image_data = content;
                        }
                    }
                }
            }

            if(!title.empty() && !description.empty() && !uploader.empty() && !image_data.empty()){
                // Process the received data
                std::cout << "Title: " << title << std::endl;
                std::cout << "Description: " << description << std::endl;
                std::cout << "Uploader: " << uploader << std::endl;
                std::cout << "Image size: " << image_data.size() << " bytes" << std::endl;

                // Save the image to a file (example)
                std::ofstream image_file("uploaded_image.jpg", std::ios::binary);
                image_file.write(image_data.c_str(), image_data.size());
                image_file.close();

                mg_printf(conn, "HTTP/1.1 200 OK\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "Content-Type: application/json\r\n");
                mg_printf(conn, "\r\n");
                mg_printf(conn, "{\"status\":\"success\"}");
                return 200;
            }else{
                std::cerr << "[ERROR] Missing required fields in the request." << std::endl;
                mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "\r\n");
                return 400;
            }
        }else{
            std::cerr << "[ERROR] Invalid or missing Content-Type header." << std::endl;
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 400;
        }
    }else{
        std::cerr << "[ERROR] Unsupported request method: " << (req_info->request_method ? req_info->request_method : "null") << std::endl;
        mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 405;
    }
}


int get_videos_data_handler(struct mg_connection* conn, void* data)
{
    const mg_request_info* req_info = mg_get_request_info(conn);

    if(strcmp("POST", req_info->request_method) == 0){
        char post_data[10000];
        int post_data_len = mg_read(conn, post_data, sizeof(post_data) - 1);
        post_data[post_data_len] = '\0';

        if(post_data_len > 0){
            json request_json = json::parse(post_data);

            if(!request_json.contains("videos")){
                std::cerr << "[ERROR] Missing videos parameter in request." << std::endl;
                mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "\r\n");
                return 400;
            }

            if(!request_json.contains("chat_id")){
                std::cerr << "[ERROR] Missing chat_id parameter in request." << std::endl;
                mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
                mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
                mg_printf(conn, "\r\n");
                return 400;
            }

            int64_t chat_id = std::stoll(request_json["chat_id"].get<std::string>());

            json out;

            for(const auto& video : request_json["videos"]){
                int64_t video_id = video["message_id"];

                json res = db_select("SELECT * FROM telegram_video WHERE chat_id = " + std::to_string(chat_id) + " AND video_id = " + std::to_string(video_id) + ";");

                if(res.empty()){
                    out.push_back(video);
                }else{
                    unsigned int video_id = res[0]["video_id"];

                    res = db_select("SELECT * FROM videos WHERE video_id = " + std::to_string(video_id) + ";");
                    if(res.empty()){
                        out.push_back(video);
                    }else{
                        res = res[0];
                        res["telegram_data"] = video;
                        out.push_back(res);
                    }
                }
            } 

            mg_printf(conn, "HTTP/1.1 200 OK\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "Content-Type: application/json\r\n");
            mg_printf(conn, "Content-Length: %zu\r\n", out.dump().size());
            mg_printf(conn, "\r\n");
            mg_write(conn, out.dump().c_str(), out.dump().size());
            return 200;
        }else{
            std::cerr << "[ERROR] No data received in request." << std::endl;
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 400;
        }
    }else{
        mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "Allow: POST\r\n");
        mg_printf(conn, "Content-Type: application/json\r\n");
        mg_printf(conn, "\r\n");
        return 405;
    }
}

int handle_upload(struct mg_connection* conn, void* data)
{
    const mg_request_info* req_info = mg_get_request_info(conn);
    const char *range = mg_get_header(conn, "Content-Range");
    const char *session_id_h = mg_get_header(conn, "session_id");
    const char *chat_id_h = mg_get_header(conn, "chat_id");
    const char *file_name_h = mg_get_header(conn, "file_name");
    
    std::cout << "Received connection" << std::endl;

    if(strcmp("POST", req_info->request_method) != 0){
        std::cerr << "[ERROR] Unsupported request method: " << (req_info->request_method ? req_info->request_method : "null") << std::endl;
        mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 405;
    }

    std::cout << range << std::endl;

    if(range){
        int start, end, size;
        if (sscanf(range, "bytes %d-%d/%d", &start, &end, &size) != 3){
            std::cerr << "[ERROR] Invalid Range header format" << std::endl;
            mg_printf(conn, "HTTP/1.1 416 Range Not Satisfiable\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 416;
        }
        
        std::cout << start << " " << end << " " << size << std::endl;

        std::string file_path = "tmp/" + std::string(file_name_h);
        
        std::cout << file_path << std::endl;

        if(!std::filesystem::exists("tmp")){
            std::filesystem::create_directory("tmp");
        }

        std::ofstream file;

        if(start == 0){
            file.open(file_path, std::ios::binary);
        }else{
            file.open(file_path, std::ios::binary | std::ios::app);
        }

        if(!file){
            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n");
            mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
            mg_printf(conn, "\r\n");
            return 500;
        }

        char* buffer = new char[end - start + 1];
        int bytes_read = mg_read(conn, buffer, end - start + 1);

        if(bytes_read > 0){
            file.write(buffer, bytes_read);
            file.close();
        }

        delete[] buffer;

        if(end == size - 1){
            int64_t chat_id;
            sscanf(chat_id_h, "%lld", &chat_id);
            int64_t session_id;
            sscanf(session_id_h, "%lld", &session_id);

            std::shared_ptr<ClientSession> session = getSession(session_id);

            session->send({
                {"@type", "sendMessage"},
                {"chat_id", chat_id},
                {"input_message_content", {
                  {"@type", "inputMessageVideo"},
                  {"video", {
                    {"@type", "inputFileLocal"},
                    {"path", file_path}
                  }}
                }}
              });
            
            std::cout << "[MESSAGE] Sending to chat " << chat_id << std::endl;

            uint32_t last_checked = 0;
            bool uploaded = false;
            while(!uploaded){
                auto responses = session->getResponses()->get_all(last_checked);

                for(auto r = responses.rbegin(); r != responses.rend(); r++){
                    last_checked = r->first;

                    json response = r->second;

                    if(!response.is_null() && response["@type"] == "updateFile"){
                        std::string res_file_path = response["file"]["local"]["path"];
                        bool upload_complete = response["file"]["local"]["is_downloading_completed"];

                        if(upload_complete && res_file_path == std::filesystem::absolute(file_path).string()){
                            std::cout << "[MESSAGE] Upload complete!" << std::endl;
                            std::filesystem::remove(file_path);
                            uploaded = true;
                            break;
                        }
                    }
                }

                if(!responses.empty()){
                    last_checked = responses.rbegin()->first;
                }
            }
        }

        mg_printf(conn, "HTTP/1.1 200 OK\r\n");
        mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
        mg_printf(conn, "\r\n");
        return 200;
    }
    
    mg_printf(conn, "HTTP/1.1 200 OK\r\n");
    mg_printf(conn, "Access-Control-Allow-Origin: *\r\n");
    mg_printf(conn, "\r\n");
    return 200;
}
