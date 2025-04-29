#include "auth.hpp"
#include "common.hpp"

#include <td/telegram/td_json_client.h>

#include <iostream>
#include <string>
#include <filesystem>

void td_auth_send_parameters(std::shared_ptr<ClientSession> session, const std::string& api_id, const std::string& api_hash, const std::string& directory)
{
    session->send({{"@type", "setLogVerbosityLevel"},
            {"new_verbosity_level", 0}});

    std::cout << "[MESSAGE] TDLib log verbosity level set to 0" << std::endl;

    // Start authentication process

    std::cout << "[MESSAGE] TDLib authentication started" << std::endl;

    uint32_t last_checked = 0;
    while(true){
        auto responses = session->getResponses()->get_all(last_checked);
        for(auto response = responses.begin(); response != responses.end(); response++){
            if(response->first <= last_checked){
                continue;
            }

            last_checked = response->first;
            json r = response->second;

            if(r.is_null() || !r.contains("@type")){
                continue;
            }

            if(r["@type"] == "updateAuthorizationState"){
                std::string state = r["authorization_state"]["@type"];

                std::cout << "[MESSAGE] Current state: " << state << std::endl;

                std::string final_dir = "UserData/" + directory;
                std::filesystem::create_directories(final_dir);

                // Send TDLib parameters
                if(state == "authorizationStateWaitTdlibParameters"){
                    session->send({{"@type", "setTdlibParameters"},
                                {"database_directory", final_dir},
                                {"use_message_database", true},
                                {"use_secret_chats", false},
                                {"api_id", std::stoi(api_id)},
                                {"api_hash", api_hash},
                                {"system_language_code", "en"},
                                {"device_model", "Server"},
                                {"system_version", "Linux"},
                                {"application_version", "1.0"},
                                {"enable_storage_optimizer", true},
                                {"use_test_dc", false}

                    });

                    std::cout << "[MESSAGE] TDLib parameters sent successfully!\n";

                    while(td_auth_get_state(session) == "authorizationStateWaitTdlibParameters");

                    return;
                }
            }
        }
    }

    while(td_auth_get_state(session) == "authorizationStateWaitTdlibParameters"){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void td_auth_send_number(std::shared_ptr<ClientSession> session, const std::string &phone)
{
    std::string state = "";

    session->send({{"@type", "getAuthorizationState"}});
    state = td_auth_get_state(session);

    if(state == "authorizationStateReady"){
        std::cout << "[MESSAGE] Already authorized!\n";
    }else if(state == "authorizationStateWaitPhoneNumber"){
        std::cout << "[MESSAGE] Sending phone number: " << phone << std::endl;

        session->send({{"@type", "setAuthenticationPhoneNumber"},
                 {"phone_number", phone}});

        std::cout << "[MESSAGE] Phone number sent successfully!" << std::endl;
    }

    while(td_auth_get_state(session) == "authorizationStateWaitPhoneNumber"){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void td_auth_send_code(std::shared_ptr<ClientSession> session, const std::string &code)
{
    std::string state = "";

    session->send({{"@type", "getAuthorizationState"}});
    state = td_auth_get_state(session);

    if(state == "authorizationStateReady"){
        std::cout << "[MESSAGE] Already authorized!\n";
    }else if(state == "authorizationStateWaitCode"){
        std::cout << "[MESSAGE] Sending code: " << code << std::endl;

        session->send({{"@type", "checkAuthenticationCode"},
                 {"code", code}});

        std::cout << "[MESSAGE] Code sent successfully!\n";
    }

    while(td_auth_get_state(session) == "authorizationStateWaitCode"){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void td_auth_send_password(std::shared_ptr<ClientSession> session, const std::string &password)
{
    std::string state = "";

    session->send({{"@type", "getAuthorizationState"}});
    state = td_auth_get_state(session);

    if(state == "authorizationStateReady"){
        std::cout << "[MESSAGE] Already authorized!\n";
    }else if(state == "authorizationStateWaitPassword"){
        std::cout << "[MESSAGE] Sending password: " << password << std::endl;
        session->send({{"@type", "checkAuthenticationPassword"},
            {"password", password}});
    }

    while(td_auth_get_state(session) == "authorizationStateWaitPassword"){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

std::string td_auth_get_state(std::shared_ptr<ClientSession> session)
{
    std::string state = "";

    uint32_t last_checked = 0;

    while(true){
        auto responses = session->getResponses()->get_all(last_checked);
        
        for(auto response = responses.rbegin(); response != responses.rend(); response++){
            if(response->first <= last_checked){
                continue;
            }

            json r = response->second;

            if(r.is_null() || !r.contains("@type")){
                continue;
            }

            if(r["@type"] == "updateAuthorizationState"){
                state = r["authorization_state"]["@type"];
                return state;
            }else if(r["@type"] == "authorizationStateWaitTdlibParameters"){
                state = "authorizationStateWaitTdlibParameters";
                return state;
            }else if(r["@type"] == "authorizationStateWaitPhoneNumber"){
                state = "authorizationStateWaitPhoneNumber";
                return state;
            }else if(r["@type"] == "authorizationStateWaitCode"){
                state = "authorizationStateWaitCode";
                return state;
            }else if(r["@type"] == "authorizationStateWaitPassword"){
                state = "authorizationStateWaitPassword";
                return state;
            }else if(r["@type"] == "authorizationStateReady"){
                state = "authorizationStateReady";
                return state;
            }else if(r["@type"] == "authorizationStateClosed"){
                state = "authorizationStateClosed";
                return state;
            }
        }

        if(responses.empty()){
            continue;
        }else{
            last_checked = responses.back().first;
        }
    }

    return state;
}