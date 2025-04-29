#include <td/telegram/td_json_client.h>
#include <civetweb.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <mutex>
#include <csignal> 

#include "auth.hpp"
#include "common.hpp"
#include "chats.hpp"
#include "app_data.hpp"
#include "endpoints.hpp"
#include "db.hpp"

std::atomic<bool> running(true);

void signal_handler(int signal)
{
    if(signal == SIGINT){
        std::cout << "\n🛑 Interruzione ricevuta (Ctrl + C). Arresto del server...\n";
        disconnect_db();
        running = false;
    }
}

int main()
{
    std::signal(SIGINT, signal_handler);

    connect_db();
    setup_endpoints();

    std::cout << "📺 Server running at http://localhost:10000\n";

    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    std::cout << "✅ Server arrestato correttamente.\n";
    return 0;
}