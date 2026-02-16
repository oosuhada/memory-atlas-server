/**
 * @file ChatRoom.cpp
 * @brief `ChatRoom` 클래스의 멤버 함수 구현부입니다.
 */

#include "ChatRoom.hpp"
#include "ChatSession.hpp" // deliver() 와 같은 SessionInterface의 구체적인 구현을 위해 필요
#include "spdlog/spdlog.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

/**
 * @details 채팅방의 이름을 초기화하고, 최대 참여 인원을 기본값으로 설정합니다.
 *          방 생성 시 로그를 남깁니다.
 */
ChatRoom::ChatRoom(const std::string &name)
    : name_(name), max_participants_(100) // 최대 인원을 100명으로 기본 설정
{
  spdlog::info("ChatRoom '{}' created.", name_);
}

/**
 * @details 채팅방이 가득 차지 않았는지 확인한 후, `participants_` 셋에 세션을 추가합니다.
 *          세션 객체 자체에도 현재 참여 중인 방의 이름을 기록합니다.
 *          입장 사실을 자신을 포함한 모든 참여자에게 브로드캐스트합니다.
 */
void ChatRoom::join(SessionPtr participant) {
  if (participants_.size() >= max_participants_) {
    participant->deliver("Error: 방 '" + name_ + "'이(가) 꽉 찼습니다.\r\n");
    return;
  }
  participants_.insert(participant);
  participant->set_current_room(name_); // 세션에 현재 방 정보 업데이트

  std::string join_msg =
      "* " + participant->nickname() + "님이 '" + name_ + "' 방에 입장했습니다.\r\n";
  broadcast(join_msg, nullptr); // 방의 모든 참여자에게 입장 사실 알림
}

/**
 * @details `participants_` 셋에서 해당 세션을 제거합니다.
 *          성공적으로 제거되면, 세션의 현재 방 정보를 초기화하고,
 *          퇴장 사실을 남은 모든 참여자에게 브로드캐스트합니다.
 */
void ChatRoom::leave(SessionPtr participant) {
  size_t erased_count = participants_.erase(participant);
  if (erased_count > 0) {
    participant->set_current_room(""); // 세션의 방 정보 클리어

    std::string leave_msg =
        "* " + participant->nickname() + "님이 '" + name_ + "' 방에서 나갔습니다.\r\n";
    broadcast(leave_msg, nullptr); // 방에 남아있는 참여자들에게 퇴장 사실 알림
  }
}

/**
 * @details 메시지 앞에 `[방이름]` 접두사를 붙여 포맷팅합니다.
 *          `participants_` 셋을 순회하며, 메시지를 보낸 `sender`를 제외한
 *          모든 참여자에게 메시지를 `deliver`합니다.
 * @note `sender`가 `nullptr`인 경우, 시스템 메시지로 간주하여 모든 참여자에게 전송됩니다.
 */
void ChatRoom::broadcast(const std::string &message, SessionPtr sender) {
  // 방 이름을 포함하도록 메시지 포맷팅 (이 부분은 서버 로직에 따라 변경될 수 있음)
  std::string formatted_message;
    if (message.find("*") == 0) { // 시스템 메시지인 경우
        formatted_message = message;
    } else {
        formatted_message = "[" + (sender ? sender->nickname() : "system") + " @ " + name_ + "]: " + message + "\r\n";
    }

  for (const auto &participant : participants_) {
    // sender가 nullptr (시스템 메시지) 이거나, participant가 sender가 아닌 경우에만 전송
    if (participant != sender) {
      participant->deliver(formatted_message);
    }
  }
}

/**
 * @details `participants_` 셋을 순회하며 각 세션의 `nickname()`을 호출하여
 *          닉네임 목록을 `std::vector<std::string>` 형태로 만들어 반환합니다.
 */
std::vector<std::string> ChatRoom::get_participant_nicknames() const {
  std::vector<std::string> nicknames;
  nicknames.reserve(participants_.size());
  for (const auto &p : participants_) {
    nicknames.push_back(p->nickname());
  }
  return nicknames;
}

/**
 * @details 현재 참여자 수(`participants_.size()`)가 최대 참여 가능 인원(`max_participants_`)보다
 *          크거나 같은지 비교하여 결과를 반환합니다.
 */
bool ChatRoom::is_full() const {
  return participants_.size() >= max_participants_;
}

// sessions() 함수는 헤더에 이미 인라인으로 정의되어 있으므로 여기서는 구현하지 않음