/**
 * @file ChatRoom.hpp
 * @brief 채팅방의 기능과 상태를 관리하는 `ChatRoom` 클래스를 정의합니다.
 */
#pragma once

#include "SessionInterface.hpp" // SessionPtr을 위해 SessionInterface 포함
#include <memory>
#include <set>
#include <string>
#include <vector>

/**
 * @class ChatRoom
 * @brief 단일 채팅방을 나타내는 클래스.
 * @details 채팅방의 이름, 참여자 목록을 관리하고, 해당 방에만 메시지를 브로드캐스트하는 기능을 제공합니다.
 *          `ChatServer`에 의해 생성되고 관리됩니다.
 */
class ChatRoom {
private:
    std::string name_; ///< 채팅방의 고유한 이름
    std::set<SessionPtr> participants_; ///< 채팅방에 참여 중인 세션들의 집합. `SessionPtr`은 `std::shared_ptr<SessionInterface>`입니다.
    size_t max_participants_; ///< 최대 참여 가능 인원 수
    // ChatServer에 대한 참조가 필요하다면 추가
    // ChatServer& server_; 

public:
    /**
     * @brief ChatRoom 생성자.
     * @param name 생성할 채팅방의 이름.
     */
    explicit ChatRoom(const std::string& name);

    /**
     * @brief 세션을 채팅방에 참여시킵니다.
     * @param participant 참여할 세션의 `SessionPtr`.
     */
    void join(SessionPtr participant);

    /**
     * @brief 세션을 채팅방에서 퇴장시킵니다.
     * @param participant 퇴장할 세션의 `SessionPtr`.
     */
    void leave(SessionPtr participant);

    /**
     * @brief `join`의 별칭(alias)입니다.
     * @param participant 참여할 세션.
     */
    void add_participant(SessionPtr participant) { join(participant); }  // alias for join

    /**
     * @brief `leave`의 별칭(alias)입니다.
     * @param participant 퇴장할 세션.
     */
    void remove_participant(SessionPtr participant) { leave(participant); } // alias for leave

    /**
     * @brief 채팅방에 참여한 모든 세션에게 메시지를 브로드캐스트합니다.
     * @param message 전송할 메시지 내용.
     * @param sender 메시지를 보낸 세션. 이 세션에는 메시지가 다시 전송되지 않습니다.
     */
    void broadcast(const std::string& message, SessionPtr sender);

    /**
     * @brief 현재 채팅방에 참여 중인 모든 사용자의 닉네임 목록을 반환합니다.
     * @return std::vector<std::string> 닉네임 목록.
     */
    std::vector<std::string> get_participant_nicknames() const;

    /**
     * @brief 채팅방이 최대 인원에 도달했는지 확인합니다.
     * @return bool 가득 찼으면 `true`, 아니면 `false`.
     */
    bool is_full() const;

    /**
     * @brief 채팅방이 비어있는지 확인합니다.
     * @return bool 비어있으면 `true`, 아니면 `false`.
     */
    bool empty() const { return participants_.empty(); }

    /**
     * @brief 채팅방의 이름을 반환합니다.
     * @return const std::string& 채팅방 이름.
     */
    const std::string& name() const { return name_; }

    /**
     * @brief 현재 참여자 수를 반환합니다.
     * @return size_t 참여자 수.
     */
    size_t participant_count() const { return participants_.size(); }

    /**
     * @brief 참여자 세션 목록을 반환합니다.
     * @return const std::set<SessionPtr>& 참여자 세션의 집합.
     */
    const std::set<SessionPtr>& sessions() const { return participants_; }
};