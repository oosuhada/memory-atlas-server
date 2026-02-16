// src/MessageHistory.cpp
#include "MessageHistory.hpp" // Include the header for the class definition
#include "spdlog/spdlog.h"     // Include spdlog for logging
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <iomanip>

namespace fs = std::filesystem;

// 스레드 안전성을 위한 뮤텍스
static std::mutex history_mutex;

// 전방 선언
namespace {
    std::string get_timestamp();
    std::vector<std::string> read_last_lines(const std::string& filename, size_t limit);
}

//------------------------------------------------------------------------------
// MessageHistory Implementation
//------------------------------------------------------------------------------
MessageHistory::MessageHistory(const std::string &history_dir) : history_dir_(history_dir)
{
    try {
        // history_dir가 존재하지 않으면 생성
        if (!fs::exists(history_dir_)) {
            fs::create_directories(history_dir_);
        }
        
        // 글로벌 히스토리, 개인 메시지, 채팅방 히스토리 디렉토리 생성
        fs::create_directories(history_dir_ + "/global");
        fs::create_directories(history_dir_ + "/private");
        fs::create_directories(history_dir_ + "/rooms");
        
        enabled_ = true;
        spdlog::info("MessageHistory initialized with directory: {}", history_dir_);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to initialize MessageHistory: {}", e.what());
        enabled_ = false;
    }
}

MessageHistory::~MessageHistory()
{
    spdlog::info("MessageHistory destroyed");
}

void MessageHistory::log_global_message(const std::string &message, const std::string &sender)
{
    if (!enabled_)
        return;
    
    try {
        std::string timestamp = get_timestamp();
        std::string log_entry = timestamp + " [" + (sender.empty() ? "system" : sender) + "]: " + message;
        
        std::lock_guard<std::mutex> lock(history_mutex);
        std::ofstream file(history_dir_ + "/global/history.txt", std::ios::app);
        if (file.is_open()) {
            file << log_entry << std::endl;
            file.close();
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to log global message: {}", e.what());
    }
}

void MessageHistory::log_private_message(const std::string &message, const std::string &sender, const std::string &receiver)
{
    if (!enabled_)
        return;
    
    try {
        std::string timestamp = get_timestamp();
        std::string log_entry = timestamp + " [" + sender + " -> " + receiver + "]: " + message;
        
        // 두 사용자 ID를 알파벳 순으로 정렬하여 일관된 파일명 생성
        std::string user1 = sender;
        std::string user2 = receiver;
        if (user1 > user2) std::swap(user1, user2);
        
        std::string filename = history_dir_ + "/private/" + user1 + "_" + user2 + ".txt";
        
        std::lock_guard<std::mutex> lock(history_mutex);
        std::ofstream file(filename, std::ios::app);
        if (file.is_open()) {
            file << log_entry << std::endl;
            file.close();
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to log private message: {}", e.what());
    }
}

void MessageHistory::log_room_message(const std::string &room_name, const std::string &message, const std::string &sender)
{
    if (!enabled_)
        return;
    
    try {
        std::string timestamp = get_timestamp();
        std::string log_entry = timestamp + " [" + (sender.empty() ? "system" : sender) + "]: " + message;
        
        std::string filename = history_dir_ + "/rooms/" + room_name + ".txt";
        
        std::lock_guard<std::mutex> lock(history_mutex);
        std::ofstream file(filename, std::ios::app);
        if (file.is_open()) {
            file << log_entry << std::endl;
            file.close();
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to log room message: {}", e.what());
    }
}

std::vector<std::string> MessageHistory::load_global_history(size_t limit)
{
    std::vector<std::string> result;
    if (!enabled_) return result;
    
    try {
        std::lock_guard<std::mutex> lock(history_mutex);
        std::string filename = history_dir_ + "/global/history.txt";
        
        if (!fs::exists(filename)) return result;
        
        result = read_last_lines(filename, limit);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to load global history: {}", e.what());
    }
    
    return result;
}

std::vector<std::string> MessageHistory::load_private_history(const std::string &user1, const std::string &user2, size_t limit)
{
    std::vector<std::string> result;
    if (!enabled_) return result;
    
    try {
        // 두 사용자 ID를 알파벳 순으로 정렬하여 일관된 파일명 생성
        std::string u1 = user1;
        std::string u2 = user2;
        if (u1 > u2) std::swap(u1, u2);
        
        std::string filename = history_dir_ + "/private/" + u1 + "_" + u2 + ".txt";
        
        if (!fs::exists(filename)) return result;
        
        std::lock_guard<std::mutex> lock(history_mutex);
        result = read_last_lines(filename, limit);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to load private history: {}", e.what());
    }
    
    return result;
}

std::vector<std::string> MessageHistory::load_room_history(const std::string &room_name, size_t limit)
{
    std::vector<std::string> result;
    if (!enabled_) return result;
    
    try {
        std::string filename = history_dir_ + "/rooms/" + room_name + ".txt";
        
        if (!fs::exists(filename)) return result;
        
        std::lock_guard<std::mutex> lock(history_mutex);
        result = read_last_lines(filename, limit);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to load room history: {}", e.what());
    }
    
    return result;
}

// 익명 네임스페이스 내의 유틸리티 함수 구현
namespace {
    // 타임스탬프 생성
    std::string get_timestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        struct tm timeinfo;
        
#ifdef _MSC_VER
        localtime_s(&timeinfo, &time);
#else
        localtime_r(&time, &timeinfo);
#endif
        
        ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    // 파일의 마지막 N줄 읽기
    std::vector<std::string> read_last_lines(const std::string& filename, size_t limit)
    {
        std::vector<std::string> lines;
        std::ifstream file(filename);
        
        if (!file.is_open()) return lines;
        
        // 파일의 모든 줄을 읽어옴
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        
        // limit가 0이거나 라인 수보다 크면 모든 줄 반환
        if (limit == 0 || limit >= lines.size()) {
            return lines;
        }
        
        // 마지막 limit 줄만 반환
        return std::vector<std::string>(lines.end() - limit, lines.end());
    }
}