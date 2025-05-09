#include "endpoints.hpp"
#include "auth.hpp"
#include "common.hpp"
#include "chats.hpp"
#include "app_data.hpp"
#include "session.hpp"
#include "random.hpp"
#include "db.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <iostream>
#include <fstream>
#include <algorithm>

void setup_endpoints()
{
    std::cout << "Setting up endpoints" << std::endl;

    httplib::SSLServer svr("./cert.pem", "./key.pem");

    std::cout << "Server initialized" << std::endl;

    // telegram routes
    svr.Get("/get_files", handle_get_files);
    svr.Get("/video", handle_video);
    svr.Get("/auth", handle_auth);
    svr.Get("/auth/get_state", handle_get_state);
    svr.Get("/logout", handle_logout);
    svr.Get("/get_chats", handle_chats);
    svr.Post("/upload", handle_upload);

    // db routes
    svr.Post("/set_video_data", set_video_data_handler);
    svr.Get("/get_videos_data", get_videos_data_handler);

    svr.listen("0.0.0.0", 10000);

    std::cout << "Server running on HTTPS port 10000" << std::endl;
}

int handle_chats(const httplib::Request& req, httplib::Response& res)
{
    // Parse query string da req (in httplib è già divisa)
    uint32_t session_id = 0;

    auto it = req.get_param_value("session_id");
    if (!it.empty()) {
        session_id = std::stoul(it);
    }
    else {
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        return 400;
    }

    std::vector<json> chats = get_chats(getSession(session_id));
    std::string json_str = json(chats).dump();

    res.status = 200;
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Content-Type", "application/json");
    res.set_content(json_str, "application/json");
    return 200;
}

int handle_video(const httplib::Request& req, httplib::Response& res)
{
    const char* range_header = req.get_header_value("Range").c_str();

    if (range_header && *range_header) {
        size_t start = 0, end = 0;
        unsigned int file_id = 0;
        uint32_t session_id = 0;

        // Estrai i parametri dalla query string
        if (req.has_param("file_id")) {
            file_id = std::stoul(req.get_param_value("file_id"));
        }
        else {
            std::cerr << "[ERROR] Missing file_id parameter in request." << std::endl;
            res.status = 400;
            res.set_header("Access-Control-Allow-Origin", "*");
            return 400;
        }

        if (req.has_param("session_id")) {
            session_id = std::stoul(req.get_param_value("session_id"));
        }
        else {
            std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
            res.status = 400;
            res.set_header("Access-Control-Allow-Origin", "*");
            return 400;
        }

        // Estrai il range
        int matched = sscanf(range_header, "bytes=%zu-%zu", &start, &end);
        if (matched == 1) {
            end = start + 1024 * 1024 - 1;
        }
        else if (matched != 2) {
            std::cerr << "❌ Invalid Range header format" << std::endl;
            std::cerr << "📥 Range Header: " << range_header << std::endl;
            res.status = 416;
            res.set_header("Access-Control-Allow-Origin", "*");
            return 416;
        }

        std::shared_ptr<ClientSession> session = getSession(session_id);

        session->send({
            {"@type", "downloadFile"},
            {"file_id", file_id},
            {"priority", 1},
            {"offset", start},
            {"limit", end - start + 1},
            {"synchronous", false}
            });

        static std::map<std::pair<unsigned int, uint32_t>, uint32_t> last_checked_map;
        std::pair<unsigned int, uint32_t> key = std::make_pair(file_id, session_id);

        while (true) {
            auto responses = session->getResponses()->get_all(last_checked_map[key]);

            for (auto r = responses.begin(); r != responses.end(); ++r) {
                last_checked_map[key] = r->first;
                json response = r->second;

                if (!response.is_null() && response["@type"] == "updateFile") {
                    if (response["file"]["id"] == file_id &&
                        response["file"]["local"]["is_downloading_active"] == false)
                    {
                        size_t available_start = response["file"]["local"]["download_offset"];
                        size_t available_size = response["file"]["local"]["downloaded_size"];
                        size_t available_end = available_start + available_size - 1;

                        end = std::min({ end, (size_t)response["file"]["expected_size"] - 1, available_end });

                        std::string file_path = response["file"]["local"]["path"];
                        int file_size = response["file"]["expected_size"];
                        std::ifstream file(file_path, std::ios::binary);

                        if (file) {
                            size_t length = end - start + 1;
                            file.seekg(start);
                            std::vector<char> buffer(length);
                            file.read(buffer.data(), length);
                            file.close();

                            res.status = 206;
                            res.set_header("Access-Control-Allow-Origin", "*");
                            res.set_header("Content-Type", "video/mp4");
                            res.set_header("Accept-Ranges", "bytes");
                            res.set_header("Content-Length", std::to_string(length));
                            res.set_header("Content-Range", "bytes " + std::to_string(start) + "-" +
                                std::to_string(end) + "/" + std::to_string(file_size));
                            res.set_content(std::string(buffer.begin(), buffer.end()), "video/mp4");
                            return 200;
                        }
                        else {
                            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
                            res.status = 500;
                            res.set_header("Access-Control-Allow-Origin", "*");
                            return 500;
                        }
                    }
                }
            }
        }
    }
    else {
        std::cerr << "[ERROR] Range header not found" << std::endl;
        res.status = 405;
        res.set_header("Access-Control-Allow-Origin", "*");
        return 405;
    }

    return 200;
}

int handle_get_files(const httplib::Request& req, httplib::Response& res)
{
    uint32_t session_id = 0;
    std::string chat_id;

    // Check and extract the session_id parameter
    if (req.has_param("session_id")) {
        session_id = std::stoul(req.get_param_value("session_id"));
    }
    else {
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        return 400;
    }

    // Check and extract the chat_id parameter
    if (req.has_param("chat_id")) {
        chat_id = req.get_param_value("chat_id");
    }
    else {
        std::cerr << "[ERROR] Missing chat_id parameter in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        return 400;
    }

    std::shared_ptr<ClientSession> session = getSession(session_id);
    std::vector<json> videos = get_videos_from_channel(session, chat_id, 0, 100);

    json files_json;
    for (const auto& video : videos) {
        files_json.push_back(video);
    }

    std::string json_str = files_json.dump();

    res.status = 200;
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Content-Type", "application/json");
    res.set_header("Content-Length", std::to_string(json_str.size()));
    res.set_content(json_str, "application/json");

    return 200;
}

int handle_auth(const httplib::Request& req, httplib::Response& res)
{
    if (req.method == "POST") {
        // Get the POST data
        std::string post_data = req.body;

        if (!post_data.empty()) {
            // Parse the JSON from POST data
            json request_json = json::parse(post_data);

            if (request_json.contains("start_auth")) { // Initiate authentication process
                uint32_t session_id = 0;

                if (request_json.contains("session_id") && request_json["session_id"] != 0) { // Client already logged in
                    session_id = std::stoul(request_json["session_id"].get<std::string>());
                }
                else { // New client, generate session id
                    session_id = rand_uint32();
                }

                std::shared_ptr<ClientSession> session = getSession(session_id);
                std::string directory = std::to_string(session_id);

                session->send({ {"@type", "getAuthorizationState"} });
                td_auth_send_parameters(session, APP_API_ID, APP_API_HASH, directory);

                // Set response headers and content
                res.status = 200;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Content-Type", "application/json");

                json response_json = { {"status", "success"}, {"session_id", session_id} };
                std::string response_str = response_json.dump();
                res.set_content(response_str, "application/json");

                return 200;
            }

            // To continue the authentication, the client must have a session open
            if (!request_json.contains("session_id")) {
                std::cerr << "[ERROR] Missing session_id in JSON." << std::endl;
                res.status = 400;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"status\": \"error\", \"message\": \"Missing session_id\"}", "application/json");
                return 400;
            }

            uint32_t session_id = std::stoul(request_json["session_id"].get<std::string>());
            std::shared_ptr<ClientSession> session = getSession(session_id);

            if (request_json.contains("phone_number")) {
                std::string phone_number = request_json["phone_number"];
                td_auth_send_number(session, phone_number);

                res.status = 200;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"status\": \"success\"}", "application/json");

                return 200;
            }
            else if (request_json.contains("code")) {
                std::string code = request_json["code"];
                td_auth_send_code(session, code);

                res.status = 200;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"status\": \"success\"}", "application/json");

                return 200;
            }
            else if (request_json.contains("password")) {
                std::string password = request_json["password"];
                td_auth_send_password(session, password);

                res.status = 200;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"status\": \"success\"}", "application/json");

                return 200;
            }
            else {
                std::cerr << "[ERROR] Missing required fields in JSON." << std::endl;
                res.status = 400;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"status\": \"error\", \"message\": \"Missing required fields\"}", "application/json");
                return 400;
            }
        }
        else {
            std::cerr << "[ERROR] No POST data received." << std::endl;
            res.status = 400;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Content-Type", "application/json");
            res.set_content("{\"status\": \"error\", \"message\": \"No POST data received\"}", "application/json");
            return 400;
        }
    }
    else {
        std::cerr << "[ERROR] Unsupported request method: " << req.method << std::endl;
        res.status = 405;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        res.set_content("{\"status\": \"error\", \"message\": \"Method Not Allowed\"}", "application/json");
        return 405;
    }
}

int handle_get_state(const httplib::Request& req, httplib::Response& res)
{
    uint32_t session_id = 0;

    // Extract the session_id parameter
    if (req.has_param("session_id")) {
        session_id = std::stoul(req.get_param_value("session_id"));
    }
    else {
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"status\": \"error\", \"message\": \"Missing session_id parameter\"}", "application/json");
        return 400;
    }

    std::shared_ptr<ClientSession> session = getSession(session_id);

    // Send request to get the authorization state
    session->send({ {"@type", "getAuthorizationState"} });
    json state_json = { {"state", td_auth_get_state(session)} };  // Assuming this function returns the state

    std::string json_str = state_json.dump();

    // Set response headers and content
    res.status = 200;
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Content-Type", "application/json");
    res.set_header("Content-Length", std::to_string(json_str.size()));
    res.set_content(json_str, "application/json");

    return 200;
}

int handle_logout(const httplib::Request& req, httplib::Response& res)
{
    uint32_t session_id = 0;

    if(req.has_param("session_id")){
        session_id = std::stoul(req.get_param_value("session_id"));
    }else{
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"status\": \"error\", \"message\": \"Missing session_id parameter\"}", "application/json");
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

    res.status = 200;
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Content-Type", "application/json");
    res.set_content("{\"status\": \"success\"}", "application/json");

    return 200;
}

//DA SALVARE NEL DB (ANCORA DA TESTARE)
int set_video_data_handler(const httplib::Request& req, httplib::Response& res)
{
    if(req.method == "POST"){
        std::string content_type(128, '\0');
        std::string content_type_header = req.get_header_value("Content-Type");

        if(!content_type_header.empty() && sscanf(content_type_header.c_str(), "multipart/form-data; boundary=%127s", content_type.data()) == 1) {
            std::string boundary = "--" + content_type;
            std::string current_part;

            // Memorizza i dati del form
            std::string title, description, uploader, image_data;

            // Il corpo della richiesta è già disponibile in req.body
            current_part = req.body;

            // Processa ciascuna parte del form
            size_t boundary_pos = 0;
            while ((boundary_pos = current_part.find(boundary)) != std::string::npos) {
                std::string part = current_part.substr(0, boundary_pos);
                current_part.erase(0, boundary_pos + boundary.size());

                // Trova l'end del header
                size_t header_end = part.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    std::string headers = part.substr(0, header_end);
                    std::string content = part.substr(header_end + 4);

                    // Analizza il campo "title"
                    if (headers.find("Content-Disposition: form-data; name=\"title\"") != std::string::npos) {
                        title = content;
                    }
                    // Analizza il campo "description"
                    else if (headers.find("Content-Disposition: form-data; name=\"description\"") != std::string::npos) {
                        description = content;
                    }
                    // Analizza il campo "uploader"
                    else if (headers.find("Content-Disposition: form-data; name=\"uploader\"") != std::string::npos) {
                        uploader = content;
                    }
                    // Analizza il campo "image"
                    else if (headers.find("Content-Disposition: form-data; name=\"image\"") != std::string::npos) {
                        image_data = content;
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

                res.status = 200;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"status\": \"success\"}", "application/json");
                return 200;
            }else{
                std::cerr << "[ERROR] Missing required parameters in request." << std::endl;
                res.status = 400;
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content("{\"status\": \"error\", \"message\": \"Missing required parameters\"}", "application/json");
                return 400;
            }
        }else{
            std::cerr << "[ERROR] Missing Content-Type header in request." << std::endl;
            res.status = 400;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("{\"status\": \"error\", \"message\": \"Missing Content-Type header\"}", "application/json");
            return 400;
        }
    }else{
        std::cerr << "[ERROR] Unsupported request method " << req.method << std::endl;
        res.status = 405;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"status\": \"error\", \"message\": \"Unsupported request method " + req.method + "\"}", "application/json");
        return 405;
    }
}

int get_videos_data_handler(const httplib::Request& req, httplib::Response& res)
{
    std::string post_data = req.body;

    // Check if the body is not empty
    if (!post_data.empty()) {
        json request_json = json::parse(post_data);

        // Check for "videos" parameter in the request
        if (!request_json.contains("videos")) {
            std::cerr << "[ERROR] Missing videos parameter in request." << std::endl;
            res.status = 400;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("{\"error\": \"Missing videos parameter\"}", "application/json");
            return 400;
        }

        // Check for "chat_id" parameter in the request
        if (!request_json.contains("chat_id")) {
            std::cerr << "[ERROR] Missing chat_id parameter in request." << std::endl;
            res.status = 400;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("{\"error\": \"Missing chat_id parameter\"}", "application/json");
            return 400;
        }

        int64_t chat_id = std::stoll(request_json["chat_id"].get<std::string>());
        json out;

        // Process each video in the "videos" array
        for (const auto& video : request_json["videos"]) {
            int64_t video_id = video["message_id"];

            // Query the database for the video
            json res_db = db_select("SELECT * FROM telegram_video WHERE chat_id = " + std::to_string(chat_id) + " AND video_id = " + std::to_string(video_id) + ";");

            // If no result, add the video to the response
            if (res_db.empty()) {
                out.push_back(video);
            }
            else {
                unsigned int db_video_id = res_db[0]["video_id"];

                // Query the video data
                res_db = db_select("SELECT * FROM videos WHERE video_id = " + std::to_string(db_video_id) + ";");
                if (res_db.empty()) {
                    out.push_back(video);
                }
                else {
                    res_db = res_db[0];
                    res_db["telegram_data"] = video;
                    out.push_back(res_db);
                }
            }
        }

        // Send response
        res.status = 200;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        res.set_content(out.dump(), "application/json");
        return 200;
    }
    else {
        std::cerr << "[ERROR] No data received in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"error\": \"No data received in request\"}", "application/json");
        return 400;
    }
}

int handle_upload(const httplib::Request& req, httplib::Response& res)
{
    // Extract headers from the request
    auto range = req.get_header_value("Content-Range");
    auto session_id_h = req.get_header_value("session_id");
    auto chat_id_h = req.get_header_value("chat_id");
    auto file_name_h = req.get_header_value("file_name");

    std::cout << "Received connection" << std::endl;

    if (req.method != "POST") {
        std::cerr << "[ERROR] Unsupported request method: " << req.method << std::endl;
        res.status = 405;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("Method Not Allowed", "text/plain");
        return 405;
    }

    std::cout << range << std::endl;

    if (!range.empty()) {
        int start, end, size;
        if (sscanf(range.c_str(), "bytes %d-%d/%d", &start, &end, &size) != 3) {
            std::cerr << "[ERROR] Invalid Range header format" << std::endl;
            res.status = 416;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("Range Not Satisfiable", "text/plain");
            return 416;
        }

        std::cout << start << " " << end << " " << size << std::endl;

        std::string file_path = "tmp/" + std::string(file_name_h);

        std::cout << file_path << std::endl;

        if (!std::filesystem::exists("tmp")) {
            std::filesystem::create_directory("tmp");
        }

        std::ofstream file;

        if (start == 0) {
            file.open(file_path, std::ios::binary);
        }
        else {
            file.open(file_path, std::ios::binary | std::ios::app);
        }

        if (!file) {
            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
            res.status = 500;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("Internal Server Error", "text/plain");
            return 500;
        }

        // Write the received chunk to the file
        file.write(req.body.c_str(), req.body.size());
        file.close();

        if (end == size - 1) {
            // Parse chat_id and session_id
            int64_t chat_id = std::stoll(chat_id_h);
            int64_t session_id = std::stoll(session_id_h);

            std::shared_ptr<ClientSession> session = getSession(session_id);

            // Sending video file to the chat
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

            // Wait for upload completion
            while (!uploaded) {
                auto responses = session->getResponses()->get_all(last_checked);

                for (auto r = responses.rbegin(); r != responses.rend(); r++) {
                    last_checked = r->first;

                    nlohmann::json response = r->second;

                    if (!response.is_null() && response["@type"] == "updateFile") {
                        std::string res_file_path = response["file"]["local"]["path"];
                        bool upload_complete = response["file"]["local"]["is_downloading_completed"];

                        if (upload_complete && res_file_path == std::filesystem::absolute(file_path).string()) {
                            std::cout << "[MESSAGE] Upload complete!" << std::endl;
                            std::filesystem::remove(file_path);
                            uploaded = true;
                            break;
                        }
                    }
                }

                if (!responses.empty()) {
                    last_checked = responses.rbegin()->first;
                }
            }
        }

        res.status = 200;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("OK", "text/plain");
        return 200;
    }

    res.status = 200;
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_content("OK", "text/plain");
    return 200;
}