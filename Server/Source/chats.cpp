#include "common.hpp"
#include "chats.hpp"

#include <iostream>

std::vector<json> get_chats(std::shared_ptr<ClientSession> session)
{
    std::vector<json> chats;
    std::map<uint32_t, uint32_t> last_checked_map;

    session->send({
        {"@type", "getChats"},
        {"offset_order", std::to_string(std::numeric_limits<uint64_t>::max())},
        {"offset_chat_id", 0},
        {"limit", std::numeric_limits<int>::max()}
    });

    bool received = false;
    uint32_t session_id = session->getId();

    while(!received){
        auto responses = session->getResponses()->get_all(last_checked_map[session_id]);

        for(auto r = responses.rbegin(); r != responses.rend(); r++){
            json response = r->second;
            if(response.is_null()) continue;

            if(response["@type"] == "chats"){
                const auto& chat_ids = response["chat_ids"];

                if(chat_ids.empty()){
                    return std::move(chats); // niente più chat
                }

                for(const auto& chat_id : chat_ids){
                    session->send({{"@type", "getChat"}, 
                                   {"chat_id", chat_id}});

                    bool found = false;
                    size_t null_ctr = 0;
                    while(!found){
                        auto chat_responses = session->getResponses()->get_all(last_checked_map[session_id]);
                        for(auto cr = chat_responses.rbegin(); cr != chat_responses.rend(); cr++){
                            json chat = cr->second;

                            if(chat.is_null()){
                                null_ctr++;
                                if(null_ctr > 5){
                                    found = true;
                                    break;
                                }
                                continue;
                            }

                            null_ctr = 0;

                            if(chat["@type"] == "chat"){
                                json chat_info = {
                                    {"id", chat["id"]},
                                    {"title", chat["title"]},
                                    {"type", chat["type"]["@type"]}
                                };
                                chats.push_back(chat_info);
                                found = true;
                                break;
                            }
                        }

                        last_checked_map[session_id] = chat_responses.back().first;
                    }
                }

                received = true;
                break; 
            }
        }
    }

    return std::move(chats);
}

std::vector<json> get_videos_from_channel(std::shared_ptr<ClientSession> session, const std::string &chat_id, int64_t from_message_id, int limit)
{
    std::vector<json> videos;
    static std::map<uint32_t, uint32_t> last_checked_map;

    session->send({{"@type", "getChatHistory"},
        {"chat_id", std::stoll(chat_id)},
        {"from_message_id", from_message_id},
        {"offset", 0},
        {"limit", limit},
        {"only_local", false}});

    uint32_t session_id = session->getId();

    while(true){
        auto responses = session->getResponses()->get_all(last_checked_map[session_id]);
        for(auto r = responses.begin(); r != responses.end(); r++){
            json response = r->second;
            last_checked_map[session_id] = r->first; 

            if(!response.is_null() && (response["@type"] == "messages" || response["@type"] == "updateNewMessage" || response["@type"] == "message")){
                // Controlla se il messaggio è un video
                /*if(response["@type"] == "updateNewMessage"){
                    if(response["message"]["content"]["@type"] == "messageVideo"){
                        unsigned int file_id = response["message"]["content"]["video"]["video"]["id"];

                        std::cout << "Video id: " << file_id << std::endl;

                        videos.push_back(Video(file_id, response["message"]["content"]["video"]["video"]["mime_type"], response["message"]["content"]["video"]["video"]["file_name"]));
                    }
                }*/

                if(response["@type"] == "messages"){
                    // Scorri i messaggi e cerca i video
                    for(const auto &message : response["messages"]){
                        if(message["content"]["@type"] == "messageVideo"){
                            unsigned int file_id = message["content"]["video"]["video"]["id"];

                            std::string message_text = "";
                            if (message["content"].contains("caption") && message["content"]["caption"].contains("text")) {
                                message_text = message["content"]["caption"]["text"];
                            }

                            json video_info = {
                                {"id", file_id},
                                {"mime_type", message["content"]["video"]["mime_type"]},
                                {"file_name", message["content"]["video"]["file_name"]},
                                {"remote_id", message["content"]["video"]["video"]["remote"]["id"]},
                                {"message_text", message_text},
                                {"message_id", message["id"]}
                            };

                            videos.push_back(video_info);
                        }
                    }

                    std::cout << "Number of messages: " << response["messages"].size() << std::endl;

                    // Se non ci sono più messaggi, esci dal loop
                    if(response["messages"].empty()){
                        return std::move(videos);
                    }

                    // Aggiorna `from_message_id` per continuare a scorrere i messaggi
                    from_message_id = response["messages"].back()["id"];

                    session->send({{"@type", "getChatHistory"},
                                {"chat_id", std::stoll(chat_id)},
                                {"from_message_id", from_message_id},
                                {"offset", 0},
                                {"limit", limit},
                                {"only_local", false}});
                }
            }
        }
    }

    return std::move(videos);
}