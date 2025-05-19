#include "endpoints.hpp"
#include "auth.hpp"
#include "common.hpp"
#include "chats.hpp"
#include "app_data.hpp"
#include "session.hpp"
#include "random.hpp"
#include "db.hpp"
#include "ffmpeg.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <iostream>
#include <fstream>
#include <algorithm>

void setup_endpoints_https()
{
    std::cout << "Setting up endpoints" << std::endl;

    httplib::SSLServer svr("./cert.pem", "./key.pem");

    std::cout << "Server initialized" << std::endl;

    // telegram routes
    svr.Get("/get_files", handle_get_files);
    svr.Get("/video", handle_video);
    svr.Get("/image", handle_image);
    svr.Post("/auth", handle_auth);
    svr.Get("/auth/get_state", handle_get_state);
    svr.Get("/logout", handle_logout);
    svr.Get("/get_chats", handle_chats);
    svr.Post("/upload", handle_upload);
    svr.Get("/get_user_info", handle_get_user_info);

    // db routes
    svr.Post("/set_video_data", set_video_data_handler);
    svr.Post("/get_videos_data", get_videos_data_handler);

    svr.listen("0.0.0.0", 10000);

    std::cout << "Server running on HTTPS port 10000" << std::endl;
}

void setup_endpoints_http()
{
    std::cout << "Setting up endpoints" << std::endl;

    httplib::Server svr;
    svr.set_read_timeout(20);
    svr.set_write_timeout(20);

    std::cout << "Server initialized" << std::endl;

    // telegram routes
    svr.Get("/get_files", handle_get_files);
    svr.Get("/video", handle_video);
    svr.Get("/image", handle_image);
    svr.Post("/auth", handle_auth);
    svr.Get("/auth/get_state", handle_get_state);
    svr.Get("/logout", handle_logout);
    svr.Get("/get_chats", handle_chats);
    svr.Post("/upload", handle_upload);
    svr.Get("/get_user_info", handle_get_user_info);

    // db routes
    svr.Post("/set_video_data", set_video_data_handler);
    svr.Post("/get_videos_data", get_videos_data_handler);

    svr.listen("0.0.0.0", 10001);

    std::cout << "Server running on HTTP port 10001" << std::endl;
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

    /*getSession(session_id)->send({
        {"@type", "optimizeStorage"},
        {"size" , 0 },
        {"ttl" , 0},
        {"count" , 0},
        {"immunity_delay" , 0}
                }
    );*/

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
    //std::cout << "Received request for video" << std::endl;
    //for (const auto& header : req.headers) {
    //    std::cout << header.first << ": " << header.second << std::endl;
    //}

    std::string range_header = req.get_header_value("Range");  // <-- conserva la stringa
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
        res.set_content("{\"error\": \"Missing file_id parameter\"}", "application/json");
        return 400;
    }

    if (req.has_param("session_id")) {
        session_id = std::stoul(req.get_param_value("session_id"));
    }
    else {
        std::cerr << "[ERROR] Missing session_id parameter in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"error\": \"Missing session_id parameter\"}", "application/json");
        return 400;
    }

    // Gestione dell'header Range
    bool range_specified = false;
    if (!range_header.empty()) {
        int matched = sscanf(range_header.c_str(), "bytes=%zu-%zu", &start, &end);
        //std::cout << "Range header: " << range_header << std::endl;
        //std::cout << "Matched: " << matched << std::endl;

        if (matched == 1) {
            if (start == 0) {
                end = start + 4096 * 4096 - 1;
            }
            else {
                end = start + 4096 * 1024 - 1;
            }
            range_specified = true;
        }

        else if (matched == 2) {
            // Caso "bytes=1000-2000"
            range_specified = true;
        }
        else {
            std::cerr << "❌ Invalid Range header format" << std::endl;
            std::cerr << "📥 Range Header: " << range_header << std::endl;
            res.status = 416; // Range Not Satisfiable
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("{\"error\": \"Invalid Range header format\"}", "application/json");
            return 416;
        }
    }
    else {
        // Se il range non è specificato, manda il primo MB
        start = 0;
        end = start + 1024 * 1024 - 1;
    }

    //std::cout << "Requested Range: " << start << " - " << end << std::endl;

    std::shared_ptr<ClientSession> session = getSession(session_id);

    // Richiedi a Telegram di scaricare la porzione richiesta
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

        for (auto r = responses.rbegin(); r != responses.rend(); ++r) {
            json response = r->second;

            if (!response.is_null() && response["@type"] == "updateFile") {
                //std::cout << response.dump(4) << std::endl;
                if (response["file"]["id"] == file_id) {
                    bool downloading_active = response["file"]["local"]["is_downloading_active"];
                    size_t available_start = response["file"]["local"]["download_offset"];
                    size_t available_prefix = response["file"]["local"]["downloaded_prefix_size"];
                    size_t available_end;
                    if (available_prefix == 0) {
                        available_end = available_start; // oppure 0
                    }
                    else {
                        available_end = available_start + available_prefix - 1;
                    }

                    size_t file_size = response["file"]["expected_size"];
                    std::filesystem::path file_path = std::filesystem::u8path(response["file"]["local"]["path"].get<std::string>());

                    // Limita end alla dimensione reale del file
                    if (end >= file_size) {
                        end = file_size - 1;
                    }

                    //std::cout << "FOUND " << available_start << " " << available_end << std::endl;

                    // Verifica se la parte richiesta è già scaricata
                    if (start >= available_start && end <= available_end && !downloading_active) {
                        std::ifstream file(file_path, std::ios::binary);
                        if (!file) {
                            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
                            res.status = 500;
                            res.set_header("Access-Control-Allow-Origin", "*");
                            res.set_content("{\"error\": \"Failed to open file\"}", "application/json");
                            return 500;
                        }

                        size_t length = end - start + 1;
                        file.seekg(start);
                        std::vector<char> buffer(length);
                        file.read(buffer.data(), length);
                        size_t actually_read = file.gcount();
                        file.close();

                        if (actually_read != length) {
                            std::cerr << "[ERROR] Only read " << actually_read << " bytes instead of " << length << std::endl;
                            res.status = 500;
                            res.set_header("Access-Control-Allow-Origin", "*");
                            res.set_content("{\"error\": \"Could not read full range\"}", "application/json");
                            return 500;
                        }

                        //std::cout << "Returning 206 Partial Content " << (std::to_string(start) + "-" +
                        //    std::to_string(end) + "/" + std::to_string(file_size)) << std::endl;

                        res.status = 206;
                        res.set_header("Access-Control-Allow-Origin", "*");
                        res.set_header("Content-Type", "video/mp4");
                        res.set_header("Accept-Ranges", "bytes");
                        res.set_header("Connection", "keep-alive");
                        res.set_header("Cache-Control", "no-cache");
                        res.set_header("Content-Length", std::to_string(length));
                        res.set_header("Content-Range", "bytes " + std::to_string(start) + "-" +
                            std::to_string(end) + "/" + std::to_string(file_size));
                        res.set_content(std::string(buffer.begin(), buffer.end()), "video/mp4");
                        return 206;
                    }
                }
            }else if (!response.is_null() && response["@type"] == "file") {
                //std::cout << response.dump(4) << std::endl;
                if (response["id"] == file_id) {
                    bool downloading_active = response["local"]["is_downloading_active"];
                    size_t available_start = response["local"]["download_offset"];
                    size_t available_prefix = response["local"]["downloaded_prefix_size"];
                    size_t available_end;
                    if (available_prefix == 0) {
                        available_end = available_start; // oppure 0
                    }
                    else {
                        available_end = available_start + available_prefix - 1;
                    }
                    size_t file_size = response["expected_size"];
                    std::filesystem::path file_path = std::filesystem::u8path(response["local"]["path"].get<std::string>());

                    // Limita end alla dimensione reale del file
                    if (end >= file_size) {
                        end = file_size - 1;
                    }

                    //std::cout << "FOUND " << available_start << " " << available_end << std::endl;

                    // Verifica se la parte richiesta è già scaricata
                    if (start >= available_start && end <= available_end && !downloading_active) {
                        std::ifstream file(file_path, std::ios::binary);
                        if (!file) {
                            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
                            res.status = 500;
                            res.set_header("Access-Control-Allow-Origin", "*");
                            res.set_content("{\"error\": \"Failed to open file\"}", "application/json");
                            return 500;
                        }

                        size_t length = end - start + 1;
                        file.seekg(start - available_start);
                        std::vector<char> buffer(length);
                        file.read(buffer.data(), length);
                        size_t actually_read = file.gcount();
                        file.close();

                        if (actually_read != length) {
                            std::cerr << "[ERROR] Only read " << actually_read << " bytes instead of " << length << std::endl;
                            res.status = 500;
                            res.set_header("Access-Control-Allow-Origin", "*");
                            res.set_content("{\"error\": \"Could not read full range\"}", "application/json");
                            return 500;
                        }

                        //std::cout << "Returning 206 Partial Content " << (std::to_string(start) + "-" +
                        //    std::to_string(end) + "/" + std::to_string(file_size)) << std::endl;

                        res.status = 206;
                        res.set_header("Access-Control-Allow-Origin", "*");
                        res.set_header("Content-Type", "video/mp4");
                        res.set_header("Accept-Ranges", "bytes");
                        res.set_header("Connection", "keep-alive");
                        res.set_header("Cache-Control", "no-cache");
                        res.set_header("Content-Length", std::to_string(length));
                        res.set_header("Content-Range", "bytes " + std::to_string(start) + "-" +
                            std::to_string(end) + "/" + std::to_string(file_size));
                        res.set_content(std::string(buffer.begin(), buffer.end()), "video/mp4");
                        return 206;
                    }
                }
            }
        }

        if (!responses.empty()) {
            last_checked_map[key] = responses.rbegin()->first;
        }
    }
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
    // Get the POST data
    std::string post_data = req.body;

    if (!post_data.empty()) {
        // Parse the JSON from POST data
        json request_json = json::parse(post_data);

        if (request_json.contains("start_auth")) { // Initiate authentication process
            uint32_t session_id = 0;

            if (request_json.contains("session_id") && request_json["session_id"].get<std::string>() != "0") { // Client already logged in
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

    while(td_auth_get_state(session) != "authorizationStateClosed"){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    session->send({{"@type", "close"}});

    closeSession(session_id);

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

int set_video_data_handler(const httplib::Request& req, httplib::Response& res)
{
    if (!req.is_multipart_form_data()) {
        res.status = 400;
        res.set_content(R"({"status": "error", "message": "Content-Type must be multipart/form-data"})", "application/json");
        return 400;
    }

    std::string title, description;
    int64_t chat_id = 0, message_id = 0;

    // campi testuali
    if (req.has_file("title")) {
        title = req.get_file_value("title").content;
        title = escape_string(title);
    }
    if (req.has_file("description")) {
        description = req.get_file_value("description").content;
        description = escape_string(description);
    }
    if (req.has_file("chat_id")) {
        chat_id = std::stoll(req.get_file_value("chat_id").content);
    }
    if (req.has_file("message_id")) {
        message_id = std::stoll(req.get_file_value("message_id").content);
    }

    //immagine
    std::string original_filename, image_data, extension;
    if (req.has_file("image")) {
        std::cout << "Got file" << std::endl;
        const auto& file = req.get_file_value("image");
        std::cout << file.filename << " size " << file.content.length() << std::endl;
        original_filename = file.filename;
        extension = "." + file.filename.substr(file.filename.find('/') + 1);
        image_data = file.content;
    }

    // salvataggio immagine e inserimento nel db
    if (!title.empty() && !description.empty() && !image_data.empty()) {
        if (!std::filesystem::exists("UserData/Thumbnails")) {
            std::filesystem::create_directory("UserData/Thumbnails");
        }

        std::string uploaded_filename = "UserData/Thumbnails/" + random_string(20) + extension;
        std::ofstream image_file(uploaded_filename, std::ios::binary);
        image_file.write(image_data.c_str(), image_data.size());
        image_file.close();

        json jres = db_select("SELECT * FROM telegram_video WHERE chat_id = " + std::to_string(chat_id) + " AND message_id = " + std::to_string(message_id) + ";");
        if (jres.empty()) {
            db_execute("INSERT INTO telegram_video (chat_id, message_id) VALUES (" + std::to_string(chat_id) + ", " + std::to_string(message_id) + ");");
        }

        db_execute("INSERT INTO image(original_filename, saved_filename) VALUES ('" + original_filename + "', '" + uploaded_filename + "');");

        unsigned int image_id = std::stoul(db_select("SELECT id FROM image WHERE original_filename = '" + original_filename + "' AND saved_filename = '" + uploaded_filename + "';")[0]["id"].get<std::string>());
        unsigned int video_id = std::stoul(db_select("SELECT id FROM telegram_video WHERE chat_id = " + std::to_string(chat_id) + " AND message_id = " + std::to_string(message_id) + ";")[0]["id"].get<std::string>());

        db_execute("INSERT INTO video(title, description, data_caricamento, telegram_video_id, thumbnail_id) VALUES ('" + title + "', '" + description + "', CURDATE(), " + std::to_string(video_id) + ", " + std::to_string(image_id) + ");");

        res.status = 200;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        res.set_content("{\"status\": \"success\"}", "application/json");
        return 200;
    } else {
        std::string error_msg = "Missing required parameters: ";
        if (title.empty()) error_msg += "title ";
        if (description.empty()) error_msg += "description ";
        if (chat_id == 0) error_msg += "chat_id ";

        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(R"({"status": "error", "message": ")" + error_msg + "\"}", "application/json");
        return 400;
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
            int64_t message_id = video["message_id"];
             
            // Query the database for the video
            json res_db = db_select("SELECT * FROM telegram_video WHERE chat_id = " + std::to_string(chat_id) + " AND message_id = " + std::to_string(message_id) + ";");

            // If no result, add the video to the response
            if (res_db.empty()) {
                out.push_back(video);
            }
            else {
                unsigned int db_video_id = std::stoi(res_db[0]["id"].get<std::string>());

                // Query the video data
                res_db = db_select("SELECT * FROM video WHERE telegram_video_id = " + std::to_string(db_video_id) + " ORDER BY id DESC;");
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

static std::unordered_map<std::string, std::shared_ptr<std::mutex>> file_mutexes;
static std::unordered_map<std::string, uint64_t> preallocated_files;

int handle_upload(const httplib::Request& req, httplib::Response& res)
{
    auto range = req.get_header_value("Content-Range");
    auto session_id_h = req.get_header_value("session_id");
    auto chat_id_h = req.get_header_value("chat_id");
    auto file_name_h = req.get_header_value("file_name");

    if (req.method != "POST") {
        std::cerr << "[ERROR] Unsupported request method: " << req.method << std::endl;
        res.status = 405;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("Method Not Allowed", "text/plain");
        return 405;
    }

    if (!range.empty()) {
        int start, end, size;
        if (sscanf(range.c_str(), "bytes %d-%d/%d", &start, &end, &size) != 3) {
            std::cerr << "[ERROR] Invalid Range header format" << std::endl;
            res.status = 416;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("Range Not Satisfiable", "text/plain");
            return 416;
        }

        std::string file_path = "tmp/" + std::string(file_name_h);

        if (!std::filesystem::exists("tmp")) {
            std::filesystem::create_directory("tmp");
        }

        auto& file_mutex = file_mutexes[file_name_h];
        if (!file_mutex) file_mutex = std::make_shared<std::mutex>();

        std::unique_lock<std::mutex> lock(*file_mutex, std::defer_lock);

        lock.lock();

        if (preallocated_files.find(file_path) == preallocated_files.end()) {
            std::ofstream prealloc(file_path, std::ios::binary | std::ios::trunc);
            prealloc.seekp(size - 1);
            prealloc.write("", 1); // Preallocate size bytes
            prealloc.close();
            preallocated_files[file_path] = 0;
        }

        std::ofstream file(file_path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
            res.status = 500;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("Internal Server Error", "text/plain");
            return 500;
        }

        file.seekp(start);
        file.write(req.body.c_str(), req.body.size());

        preallocated_files[file_path] += req.body.size();

        if (preallocated_files[file_path] == size) {
            lock.unlock();

            std::string local_path = std::filesystem::absolute(file_path).string();
            // Sometimes Telegram is not able to get video metadata, so I do it here manually.
            VideoMetadata meta = get_video_metadata(local_path);
            int64_t chat_id = std::stoll(chat_id_h);
            uint32_t session_id = std::stoul(session_id_h);

            std::thread([=]() {
                std::shared_ptr<ClientSession> session = getSession(session_id);

                if ((meta.valid)) {
                    session->send({
                        {"@type", "sendMessage"},
                        {"chat_id", chat_id},
                        {"input_message_content", {
                            {"@type", "inputMessageVideo"},
                            {"video", {
                                {"@type", "inputFileLocal"},
                                {"path", local_path}
                            }},
                            {"duration", meta.duration},
                            {"width", meta.width},
                            {"height", meta.height},
                            {"supports_streaming", true}
                        }}
                    });
                }else {
                    session->send({
                        {"@type", "sendMessage"},
                        {"chat_id", chat_id},
                        {"input_message_content", {
                            {"@type", "inputMessageVideo"},
                            {"video", {
                                {"@type", "inputFileLocal"},
                                {"path", local_path}
                            }},
                            {"supports_streaming", true}
                        }}
                        });
                }

                uint32_t last_checked = 0;
                bool uploaded = false;
                int64_t message_id = 0;

                while (!uploaded || message_id == 0) {
                    auto responses = session->getResponses()->get_all(last_checked);

                    for (auto r = responses.rbegin(); r != responses.rend(); r++) {
                        json response = r->second;

                        if (!response.is_null() && response["@type"] == "updateFile") {
                            std::string res_file_path = response["file"]["local"]["path"];
                            bool upload_complete = response["file"]["remote"]["is_uploading_completed"];

                            if (!uploaded && upload_complete && res_file_path == local_path) {
                                std::filesystem::remove(local_path);
                                uploaded = true;
                            }
                        }

                        if (!response.is_null() && message_id == 0 && response["@type"] == "updateNewMessage") {
                            const json& msg = response["message"];

                            if (msg.contains("chat_id") && msg["chat_id"] == chat_id &&
                                msg.contains("content") &&
                                msg["content"].contains("video") &&
                                msg["content"]["video"]["video"]["local"]["path"] == local_path) {

                                message_id = msg["id"];

                                db_execute("INSERT INTO telegram_video(message_id, chat_id) VALUES(" + std::to_string(message_id) + ", " + std::to_string(chat_id) + ");");
                            }
                        }

                        if (message_id && uploaded) {
                            break;
                        }
                    }

                    if (!responses.empty()) {
                        last_checked = responses.rbegin()->first;
                    }
                }
                }).detach();
        }
        else {
            lock.unlock();
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

int handle_image(const httplib::Request& req, httplib::Response& res)
{
    unsigned int image_id = req.has_param("id") ? std::stoul(req.get_param_value("id")) : std::numeric_limits<unsigned int>::max();

    if (image_id == std::numeric_limits<unsigned int>::max()) {
        std::cerr << "[ERROR] Missing id parameter in request." << std::endl;
        res.status = 400;
        res.set_header("Access-Control-Allow-Origin", "*");
        return 400;
    }

    std::string file_path = db_select("SELECT saved_filename FROM image WHERE id = " + std::to_string(image_id) + ";")[0]["saved_filename"];

    if (!file_path.empty() && std::filesystem::exists(file_path)) {
        std::ifstream file(file_path, std::ios::binary);
        if (file) {
            std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            std::string mime_type = "image/" + file_path.substr(file_path.find_last_of('.') + 1);

            res.status = 200;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Content-Type", mime_type); 
            res.set_content(buffer.data(), buffer.size(), mime_type);
            return 200;
        } else {
            std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
            res.status = 500;
            res.set_header("Access-Control-Allow-Origin", "*");
            return 500;
        }
    } else {
        std::cerr << "[ERROR] File not found: " << file_path << std::endl;
        res.status = 404;
        res.set_header("Access-Control-Allow-Origin", "*");
        return 404;
    }
}

int handle_get_user_info(const httplib::Request& req, httplib::Response& res) 
{
    std::cout << "received request" << std::endl;

    std::string session_id_str = req.get_param_value("session_id");
    std::string user_id_str = req.get_param_value("user_id");
    std::string user_type = req.get_param_value("user_type"); // "user" o "chat"

    std::cout << session_id_str << " " << user_id_str << " " << user_type << std::endl;

    if (session_id_str.empty() || user_id_str.empty() || user_type.empty()) {
        res.status = 400; // Bad Request
        res.set_content("{\"error\":\"Missing parameters (session_id, user_id, or user_type)\"}", "application/json");
        return 0;
    }

    int64_t session_id;
    int64_t user_id;
    try {
        session_id = std::stoll(session_id_str);
        user_id = std::stoll(user_id_str);
    }
    catch (const std::exception& e) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid session_id or user_id format\"}", "application/json");
        return 0;
    }

    std::shared_ptr<ClientSession> session = getSession(session_id);

    if (user_type == "user") {
        session->send({
            {"@type", "getUser"},
            {"user_id", user_id}
            });
    }
    else if (user_type == "chat") {
        session->send({
            {"@type", "getChat"},
            {"chat_id", user_id}
            });
    }
    else {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid user_type (must be 'user' or 'chat')\"}", "application/json");
        return 0;
    }

    json response;
    bool response_received = false;
    uint32_t last_checked = 0;

    while (!response_received) {
        auto responses = session->getResponses()->get_all(last_checked);
        for (const auto& [id, resp] : responses) {
            last_checked = id;

            if ((user_type == "user" && resp["@type"] == "user") ||
                (user_type == "chat" && resp["@type"] == "chat")) {
                if (resp["id"].get<int64_t>() == user_id) {
                    response = resp;
                    response_received = true;
                    break;
                }
            }
        }
    }

    if (!response_received) {
        res.status = 504; // Gateway Timeout
        res.set_content("{\"error\":\"TDLib response timeout\"}", "application/json");
        return 0;
    }

    std::cout << response.dump(4) << std::endl;

    json result;
    auto process_media = [&](const json& media_data, const std::string& media_type) -> json {
        if (!std::filesystem::exists("./public/media")) {
            std::filesystem::create_directories("./public/media");
        }

        json media_info = {
            {"local", media_data.value("local", json::object())},
            {"remote", media_data.value("remote", json::object())}
        };

        // Se esiste già il file locale
        if (media_info["local"].contains("path") && !media_info["local"]["path"].is_null()) {
            std::string local_path = media_info["local"]["path"].get<std::string>();

            // Genera un nome unico per il file
            std::string file_ext = std::filesystem::path(local_path).extension().string();
            std::string saved_filename = media_type + "_" +
                std::to_string(time(nullptr)) +
                file_ext;

            // Salva nel database
            std::string query = "INSERT INTO image (saved_filename) "
                "VALUES ('" + saved_filename + "') ";

            db_execute(query);
            json db_result = db_select("SELECT id FROM image WHERE saved_filename = '" + saved_filename + "'")[0];
            if (!db_result.is_null() && db_result.contains("id")) {
                media_info["image_id"] = db_result["id"];
                media_info["url"] = "/media/" + saved_filename;

                // Copia il file nella cartella pubblica
                std::filesystem::copy(local_path, "./public/media/" + saved_filename);
            }
        }
        // Altrimenti scarica da remoto
        else if (media_info["remote"].contains("id")) {
            std::string file_id = media_info["remote"]["id"].get<std::string>();

            // Invia richiesta di download
            session->send({
                {"@type", "downloadFile"},
                {"file_id", file_id},
                {"priority", 1},
                {"synchronous", false}
                });

            // Attendi il download
            bool downloaded = false;
            uint32_t last_checked = 0;
            while (!downloaded) {
                auto responses = session->getResponses()->get_all(last_checked);
                for (const auto& [id, resp] : responses) {
                    last_checked = id;
                    if (resp["@type"] == "file" && resp["id"] == file_id &&
                        resp["local"]["is_downloading_completed"].get<bool>()) {

                        std::string local_path = resp["local"]["path"].get<std::string>();
                        std::string file_ext = std::filesystem::path(local_path).extension().string();
                        std::string saved_filename = media_type + "_" +
                            std::to_string(time(nullptr)) +
                            file_ext;

                        // Salva nel database
                        std::string query = "INSERT INTO images (saved_filename) "
                            "VALUES ('" + saved_filename + "') ";

                        db_execute(query);
                        json db_result = db_select("SELECT id FROM image WHERE saved_filename = '" + saved_filename + "'")[0];
                        if (!db_result.is_null() && db_result.contains("id")) {
                            media_info["image_id"] = db_result["id"];
                            media_info["url"] = "/media/" + saved_filename;
                            std::filesystem::copy(local_path, "./public/media/" + saved_filename);
                        }

                        downloaded = true;
                        break;
                    }
                }
            }
        }

        return media_info;
        };

    if (user_type == "user") {
        std::string username = "";
        if (response.contains("usernames")) {
            auto usernames = response["usernames"];
            if (usernames.contains("active_usernames")) {
                auto active_usernames = usernames["active_usernames"];
                if (!active_usernames.empty()) {
                    username = active_usernames[0].get<std::string>();
                }
            }
        }

        result = {
            {"type", "user"},
            {"id", response["id"]},
            {"first_name", response.value("first_name", "")},
            {"last_name", response.value("last_name", "")},
            {"username", username},
            {"phone_number", response.value("phone_number", "")}
        };

        if (!response["profile_photo"].is_null()) {
            json photo_data = process_media(response["profile_photo"]["small"], "profile");
            result["profile_photo"] = {
                {"image_id", photo_data.value("image_id", -1)},
                {"url", photo_data.value("url", "")},
                {"original_data", {
                    {"local", response["profile_photo"]["small"]["local"]},
                    {"remote", response["profile_photo"]["small"]["remote"]}
                }}
            };
        }
    }
    else if (user_type == "chat") {
        std::string username = "";
        if (response.contains("usernames")) {
            auto& usernames = response["usernames"];
            if (usernames.contains("active_usernames")) {
                auto& active_usernames = usernames["active_usernames"];
                if (active_usernames.is_array() && !active_usernames.empty()) {
                    username = active_usernames[0].get<std::string>();
                }
            }
        }

        result = {
            {"type", "chat"},
            {"id", response["id"]},
            {"title", response.value("title", "")},
            {"username", username}
        };

        if (!response["photo"].is_null()) {
            json photo_data = process_media(response["photo"]["small"], "chat_photo");
            result["photo"] = {
                {"image_id", photo_data.value("image_id", -1)},
                {"url", photo_data.value("url", "")},
                {"original_data", {
                    {"local", response["photo"]["small"]["local"]},
                    {"remote", response["photo"]["small"]["remote"]}
                }}
            };
        }
    }

    res.set_content(result.dump(), "application/json");
    res.status = 200;
    return 200;
}