// include/MessageHistory.hpp
#pragma once

#include <string>
#include <vector>
#include <memory> // Needed for unique_ptr if used elsewhere, or just general practice

/**
 * @class MessageHistory
 * @brief 채팅 메시지 기록을 파일 시스템에 저장하고 불러오는 클래스.
 *
 * 전역, 개인, 채팅방 메시지를 각각 다른 디렉토리와 파일에 저장하여 관리한다.
 * 기록 기능은 활성화/비활성화할 수 있다.
 */
class MessageHistory {
private:
    /// @brief 채팅 기록이 저장될 기본 디렉토리 경로.
    std::string history_dir_;
    /// @brief 메시지 기록 기능 활성화 여부.
    bool enabled_ = false;
public:
    /**
     * @brief MessageHistory 생성자.
     * @param history_dir 채팅 기록을 저장할 디렉토리 경로.
     */
    explicit MessageHistory(const std::string& history_dir);
    
    /**
     * @brief MessageHistory 소멸자.
     */
    ~MessageHistory();

    /**
     * @brief 전역 메시지를 기록한다.
     * @param message 기록할 메시지 내용.
     * @param sender 발신자 닉네임 (선택사항).
     */
    void log_global_message(const std::string& message, const std::string& sender = "");
    
    /**
     * @brief 개인 메시지(귓속말)를 기록한다.
     * @param message 기록할 메시지 내용.
     * @param sender 발신자 닉네임.
     * @param receiver 수신자 닉네임.
     */
    void log_private_message(const std::string& message, const std::string& sender, const std::string& receiver);
    
    /**
     * @brief 채팅방 메시지를 기록한다.
     * @param room_name 채팅방 이름.
     * @param message 기록할 메시지 내용.
     * @param sender 발신자 닉네임 (선택사항).
     */
    void log_room_message(const std::string& room_name, const std::string& message, const std::string& sender = "");

    /**
     * @brief 전역 메시지 기록을 불러온다.
     * @param limit 불러올 최대 메시지 수 (0이면 모두).
     * @return 메시지 기록 벡터.
     */
    std::vector<std::string> load_global_history(size_t limit = 0);
    
    /**
     * @brief 개인 메시지 기록을 불러온다.
     * @param user1 사용자1 닉네임.
     * @param user2 사용자2 닉네임.
     * @param limit 불러올 최대 메시지 수 (0이면 모두).
     * @return 메시지 기록 벡터.
     */
    std::vector<std::string> load_private_history(const std::string& user1, const std::string& user2, size_t limit = 0);
    
    /**
     * @brief 채팅방 메시지 기록을 불러온다.
     * @param room_name 채팅방 이름.
     * @param limit 불러올 최대 메시지 수 (0이면 모두).
     * @return 메시지 기록 벡터.
     */
    std::vector<std::string> load_room_history(const std::string& room_name, size_t limit = 0);

    /**
     * @brief 메시지 기록 기능 활성화 여부를 반환한다.
     * @return true이면 활성화, false이면 비활성화.
     */
    bool is_enabled() const { return enabled_; }
    
    /**
     * @brief 메시지 기록 기능을 활성화/비활성화한다.
     * @param enabled 활성화 여부.
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }
};
