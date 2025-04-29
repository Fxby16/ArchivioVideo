#include "session.hpp"

#include <td/telegram/td_json_client.h>
#include <iostream>

ClientSession::ClientSession(uint32_t id): id(id) 
{
    td_instance = td_json_client_create();

    if(!td_instance){
        std::cerr << "[ERROR] Failed to create TDLib client" << std::endl;
        return;
    }

    listener = std::make_unique<TelegramListener>(td_instance);
    responses = std::make_shared<CircularBuffer<json>>(100);

    listener_thread = std::thread([this] {
        listener->poll([this](json r) {
            responses->push(r);
        });
    });
}

ClientSession::~ClientSession() 
{
    if(listener){
        listener->stop();
    }

    if(listener_thread.joinable()){
        listener_thread.join();
    }

    if(td_instance){
        td_json_client_destroy(td_instance);
    }
}

std::shared_ptr<CircularBuffer<json>> ClientSession::getResponses()
{
    return responses;
}

void ClientSession::send(const json &j) 
{
    std::unique_lock<std::mutex> lock(send_mutex);

    td_send(j, td_instance);
}

std::unordered_map<uint32_t, std::shared_ptr<ClientSession>> sessions;
std::mutex sessions_mutex;

std::shared_ptr<ClientSession> getSession(uint32_t id)
{
    std::unique_lock<std::mutex> lock(sessions_mutex);
    auto it = sessions.find(id);
    if(it != sessions.end()){
        return it->second;
    }

    auto session = std::make_shared<ClientSession>(id);
    sessions[id] = session;

    return session;
}

void closeSession(uint32_t id)
{
    std::unique_lock<std::mutex> lock(sessions_mutex);
    auto it = sessions.find(id);
    if(it != sessions.end()){
        sessions.erase(it);
    }
}