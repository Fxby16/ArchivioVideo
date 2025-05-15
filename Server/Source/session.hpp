#pragma once

#include <deque>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <cassert>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <iostream>

#include "common.hpp"

template<typename T>
class CircularBuffer{
public:
    CircularBuffer(size_t size) : maxSize(size) {}

    void push(const T& item) 
    {
        std::unique_lock<std::mutex> lock(mtx);
        
        if(buffer.size() >= maxSize){
            buffer.pop_front();
        }
    
        buffer.push_back(std::make_pair(getId(), item));
        notEmpty.notify_all();
        notChecked.notify_all();
    }
    
    std::vector<std::pair<uint32_t, T>> get_all(uint32_t last_checked = 0) 
    {
        std::unique_lock<std::mutex> lock(mtx);
        notChecked.wait(lock, [this, last_checked] { return !buffer.empty() && buffer.back().first > last_checked; });

        std::vector<std::pair<uint32_t, T>> items;

        for(auto it = buffer.begin(); it != buffer.end(); it++){
            if(it->first > last_checked){
                items.push_back(*it);
            }
        }

        return items;
    }

    std::pair<uint32_t, T> pop() 
    {
        std::unique_lock<std::mutex> lock(mtx);
        notEmpty.wait(lock, [this] { return !buffer.empty(); });
    
        std::pair<uint32_t, T> item = buffer.front();
        buffer.pop_front();
    
        return item;
    }
    
    std::pair<uint32_t, T> front() 
    {
        std::unique_lock<std::mutex> lock(mtx);
        notEmpty.wait(lock, [this] { return !buffer.empty(); });
    
        return buffer.front();
    }
    
    bool empty() const 
    {
        std::unique_lock<std::mutex> lock(mtx);
        return buffer.empty();
    }

    size_t size() const 
    {
        std::unique_lock<std::mutex> lock(mtx);
        return buffer.size();
    }

    void clear() 
    {
        std::unique_lock<std::mutex> lock(mtx);
        buffer.clear();
    }

private:
    uint32_t getId() 
    {
        assert(id < UINT32_MAX);
        return id++;
    }

    std::deque<std::pair<uint32_t, T>> buffer;
    uint32_t id = 0;
    size_t maxSize;
    std::mutex mtx;
    std::condition_variable notEmpty;
    std::condition_variable notChecked;
};

class TelegramListener{
public:
    TelegramListener(void* td_instance): td_instance(td_instance) {
        assert(td_instance != nullptr);
    }
    
    void poll(std::function<void(json)> callback)
    {
        running = true;

        while(running){
            json r = td_recv(td_instance);
            if(r.is_null()){
                continue;
            }

            callback(r);
        }
    }

    void stop() 
    {
        running = false;
    }

private:
    void* td_instance = nullptr;
    std::atomic<bool> running = true;
};

class ClientSession{
public:
    ClientSession(uint32_t id);
    ~ClientSession();

    std::shared_ptr<CircularBuffer<json>> getResponses();
    void send(const json &j);

    uint32_t getId() const { return id; }

private:
    void* td_instance = nullptr;
    std::unique_ptr<TelegramListener> listener;
    std::thread listener_thread;
    std::shared_ptr<CircularBuffer<json>> responses;
    std::mutex send_mutex;
    uint32_t id = 0;
};

extern std::shared_ptr<ClientSession> getSession(uint32_t id);
extern void closeSession(uint32_t id);