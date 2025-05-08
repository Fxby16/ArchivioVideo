#include "db.hpp"

#ifdef __linux__
    #include <mysql/mysql.h>
#else
    #include <mysql.h>
#endif
#include <mutex>
#include <iostream>

static MYSQL* conn = nullptr;
static std::mutex db_mutex;
static bool db_connected = false;
static const char* db_host = "127.0.0.1";
static const char* db_user = "root";
static const char* db_password = "";
static const char* db_name = "ArchivioVideo";

int connect_db() 
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if(db_connected){
        return 0;
    }

    conn = mysql_init(nullptr);
    if(conn == nullptr){
        std::cerr << "mysql_init() failed" << std::endl;
        return -1;
    }

    if(mysql_real_connect(conn, db_host, db_user, db_password, db_name, 0, nullptr, 0) == nullptr){
        std::cerr << "mysql_real_connect() failed: " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return -1;
    }

    db_connected = true;
    return 0;
}

int disconnect_db() 
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if(conn != nullptr){
        mysql_close(conn);
        conn = nullptr;
        db_connected = false;
    }
    return 0;
}

json db_select(const std::string& query) 
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if(!db_connected){
        std::cerr << "Database not connected" << std::endl;
        return json();
    }

    if(mysql_query(conn, query.c_str())){
        std::cerr << "mysql_query() failed: " << mysql_error(conn) << std::endl;
        return json();
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if(res == nullptr){
        std::cerr << "mysql_store_result() failed: " << mysql_error(conn) << std::endl;
        return json();
    }

    int num_fields = mysql_num_fields(res);
    MYSQL_ROW row;
    json result = json::array();

    while((row = mysql_fetch_row(res))){
        json obj = json::object();
        for(int i = 0; i < num_fields; i++){
            obj[mysql_fetch_field_direct(res, i)->name] = row[i] ? row[i] : "NULL";
        }
        result.push_back(obj);
    }

    mysql_free_result(res);
    return result;
}

int db_execute(const std::string& query) 
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if(!db_connected){
        std::cerr << "Database not connected" << std::endl;
        return -1;
    }

    if(mysql_query(conn, query.c_str())){
        std::cerr << "mysql_query() failed: " << mysql_error(conn) << std::endl;
        return -1;
    }

    return 0;
}

std::string escape_string(const std::string& str) 
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if(!db_connected){
        std::cerr << "Database not connected" << std::endl;
        return str;
    }

    char* escaped_str = new char[str.length() * 2 + 1];
    mysql_real_escape_string(conn, escaped_str, str.c_str(), str.length());
    std::string result(escaped_str);
    delete[] escaped_str;

    return result;
}