#include <thread>
#include <atomic>
#include <csignal> 
#include <iostream>

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
    std::thread https_thread(setup_endpoints_https);
    std::thread http_thread(setup_endpoints_http);

    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    std::cout << "✅ Server arrestato correttamente.\n";
    return 0;
}