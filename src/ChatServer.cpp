/**
 * @file ChatServer.cpp
 * @brief `ChatServer` 클래스의 멤버 함수 구현부입니다.
 * @details 서버의 초기화, 세션 관리, 메시지 전송, 종료 등 핵심 로직을 포함합니다.
 */

#include "ChatServer.hpp"
#include "ChatSession.hpp"
// #include "ChatListener.hpp" // TCP 리스너 제거 - WebSocketListener 사용
#include "ChatRoom.hpp"
#include "MessageHistory.hpp"
#include "WebSocketSession.hpp"
#include "spdlog/spdlog.h"

#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <map>

#include <boost/asio/dispatch.hpp>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
namespace beast = boost::beast;

//------------------------------------------------------------------------------
// ChatServer Implementation
//------------------------------------------------------------------------------
/**
 * @details `io_context`와 포트 번호 등 서버 운영에 필요한 기본 정보들을 초기화합니다.
 *          종료 시그널(SIGINT, SIGTERM)을 처리할 `signal_set`을 설정하고,
 *          서버의 주요 데이터 접근을 동기화할 `strand`를 생성합니다.
 *          메시지 히스토리 관리 객체도 이 때 생성됩니다.
 */
ChatServer::ChatServer(net::io_context &ioc,
                       unsigned short port,
                       const std::string &config_file,
                       const std::string &history_dir)
    : ioc_(ioc),
      port_(port),
      signals_(ioc, SIGINT, SIGTERM),
      strand_(net::make_strand(ioc)),
      config_file_(config_file),
      history_dir_(history_dir),
      history_(std::make_unique<MessageHistory>(history_dir)),
      stopped_(false),
      require_auth_(false)
{
    spdlog::info("[ChatServer {}] Initializing for port {}", fmt::ptr(this), port_);
}

ChatServer::~ChatServer()
{
    spdlog::info("[ChatServer {}] Destructor called.", fmt::ptr(this));
}

/**
 * @details 서버가 이미 중지 상태인 경우 실행을 거부합니다.
 *          설정 파일과 사용자 정보를 로드하고, `start_listening`을 호출하여
 *          클라이언트 연결을 받을 준비를 합니다. (현재 구조에서는 WebSocket 리스너가 외부에서 생성됨)
 *          `do_await_stop`을 호출하여 정상 종료를 위한 시그널 대기를 시작합니다.
 */
void ChatServer::run()
{
    if (stopped_.load())
    {
        spdlog::error("[ChatServer {}] Cannot run, server is already stopped.", fmt::ptr(this));
        return;
    }
    // TCP 리스너가 제거되었으므로 이 체크는 더 이상 필요하지 않음

    spdlog::info("[ChatServer {}] Starting server execution...", fmt::ptr(this));

    load_config();
    load_users();

    if (!start_listening())
    {
        spdlog::error("[ChatServer {}] Failed to start listener. Aborting run().", fmt::ptr(this));
        stopped_ = true;
        return;
    }

    do_await_stop();

    spdlog::info("[ChatServer {}] Server startup sequence complete. Listening on port {}", fmt::ptr(this), port_);
}

/**
 * @details 현재 서버 아키텍처에서는 `main.cpp`에서 직접 HTTP/WebSocket 리스너를 생성하고
 *          `ChatServer`는 세션 관리 역할에 집중하므로, 이 함수는 항상 true를 반환합니다.
 */
bool ChatServer::start_listening()
{
    // TCP 리스너 제거 - WebSocket 리스너는 main.cpp에서 직접 생성
    // ChatServer는 이제 세션 관리만 담당
    spdlog::info("[ChatServer {}] No TCP listener needed. WebSocket listeners will be created externally.", fmt::ptr(this));
    return true;
}

/**
 * @details `stopped_` 플래그를 `true`로 설정하여 새로운 작업을 막습니다.
 *          `io_context`에 `post`하여 서버 종료 작업을 예약합니다. 이 작업은 다음을 포함합니다:
 *          - 시그널 핸들러 취소
 *          - `strand_`를 통해 모든 세션, 닉네임, 채팅방 정보 정리 및 각 세션의 `stop_session` 호출
 *          - 히스토리 관리자 및 기타 리소스 정리
 *          - 설정 및 사용자 정보 저장
 */
void ChatServer::stop()
{
    if (stopped_.exchange(true))
    {
        return;
    }
    spdlog::info("[ChatServer {}] Stopping server...", fmt::ptr(this));

    // TCP listener 제거됨

    signals_.cancel();

    net::post(ioc_, [this]()
              {
        signals_.cancel();
        spdlog::info("[ChatServer {}] Signals cancelled.", fmt::ptr(this));

        net::post(strand_, [this]() {
            spdlog::info("[ChatServer {}] Closing all sessions (strand context)...", fmt::ptr(this));
            auto sessions_copy = sessions_;
            sessions_.clear();
            nicknames_.clear();

            for (auto& session_ptr : sessions_copy) {
                net::dispatch(session_ptr->get_strand(), [session = session_ptr]() {
                    if (session) {
                        session->stop_session();
                    }
                });
            }

            std::lock_guard<std::mutex> lock(rooms_mutex_);
            rooms_.clear();

            spdlog::info("[ChatServer {}] Session/Room clear initiated (strand context).", fmt::ptr(this));
        });

        if (history_) {
            history_.reset();
            spdlog::info("[ChatServer {}] History reset.", fmt::ptr(this));
        }
        save_config();
        save_users();

        spdlog::info("[ChatServer {}] Server stop sequence initiated on io_context.", fmt::ptr(this)); });
}

/**
 * @details `signals_` 객체를 사용하여 비동기적으로 종료 시그널을 기다립니다.
 *          시그널을 받으면 `stop()` 메서드를 호출하여 서버를 안전하게 종료시킵니다.
 */
void ChatServer::do_await_stop()
{
    signals_.async_wait(
        [this](boost::system::error_code ec, int signo)
        {
            if (!ec)
            {
                spdlog::info("[ChatServer {}] Stop signal ({}) received.", fmt::ptr(this), signo);
                stop();
            }
        });
}

/**
 * @details `net::dispatch`를 사용하여 `strand_` 위에서 작업을 수행함으로써 스레드 안전성을 보장합니다.
 *          `sessions_` 셋에 새로운 세션을 추가하고 로그를 남깁니다.
 *          만약 세션이 초기 `remote_id`가 아닌 실제 닉네임을 가지고 있다면 (재접속 등의 경우),
 *          입장 메시지를 브로드캐스트합니다.
 */
void ChatServer::join(SessionPtr session)
{
    if (!session || stopped_)
        return;
    auto self = shared_from_this();
    net::dispatch(strand_, [this, self, session]()
                  {
        if (stopped_) return;
        sessions_.insert(session);
        spdlog::info("[ChatServer {}] Client '{}' ({}) joined. Total sessions: {}",
                fmt::ptr(this), session->nickname(), session->remote_id(), sessions_.size());
        
        // Only broadcast join message if user has set a proper nickname (not IP:PORT)
        if (session->nickname() != session->remote_id()) {
            std::string join_msg = "* 사용자 '" + session->nickname() + "'님이 입장했습니다.\r\n";
            broadcast_impl(join_msg, session);
        } });
}

/**
 * @details `net::dispatch`와 `strand_`를 사용하여 동기화된 컨텍스트에서 다음을 수행합니다:
 *          1. 세션이 참여 중인 모든 방에서 퇴장시킵니다 (`leave_all_rooms_impl`).
 *          2. 세션이 유효한 닉네임을 가졌다면, 해당 닉네임을 `nicknames_` 맵에서 제거합니다 (`unregister_nickname`).
 *          3. `sessions_` 셋에서 세션을 제거합니다.
 *          4. 세션이 유효한 닉네임을 가졌었다면, 퇴장 메시지를 전체에 브로드캐스트합니다.
 */
void ChatServer::leave(SessionPtr session)
{
    if (!session)
        return;

    std::string nickname = session->nickname();
    std::string remote_id = session->remote_id();

    auto self = shared_from_this();
    net::dispatch(strand_, [this, self, session, nickname, remote_id]() mutable
                  {
        if (stopped_) return;
        leave_all_rooms_impl(session);
        if (!nickname.empty() && nickname != remote_id) {
            unregister_nickname(nickname);
        }
        size_t erased_count = sessions_.erase(session);
        if (erased_count > 0) {
            spdlog::info("[ChatServer {}] Client '{}' ({}) left. Session erased. Total sessions: {}",
                    fmt::ptr(this), nickname, remote_id, sessions_.size());
            // Only broadcast leave message if user had set a proper nickname (not IP:PORT)
            if (!nickname.empty() && nickname != remote_id) {
                std::string leave_msg = "* 사용자 '" + nickname + "'님이 퇴장했습니다.\r\n";
                broadcast_impl(leave_msg, nullptr);
            }
        } else {
            spdlog::warn("[ChatServer {}] Client '{}' ({}) leave called, but session not found.",
                    fmt::ptr(this), nickname, remote_id);
        } });
}

/**
 * @details 이 함수는 반드시 `strand_` 위에서 실행되어야 합니다.
 *          `sessions_`의 복사본을 만들어 순회하면서, 각 세션의 `strand`에 `deliver` 작업을 `post`합니다.
 *          이는 각 세션의 쓰기 큐를 독립적으로 관리하고, `sessions_` 컬렉션의 변경에 영향을 받지 않도록 합니다.
 *          메시지 발신자는 브로드캐스트 대상에서 제외됩니다.
 */
void ChatServer::broadcast_impl(const std::string &message, const SessionPtr &sender)
{
    spdlog::debug("[ChatServer {}] Broadcasting globally (sender: {}): {}", 
                  fmt::ptr(this), 
                  sender ? sender->remote_id() : "system", // Log sender ID for clarity
                  message);
                  
    // Capture sessions by value to iterate safely even if sessions_ modified concurrently (though protected by strand)
    auto sessions_copy = sessions_;
    
    for (const auto &session_ptr : sessions_copy)
    {
        // Check if the session is valid and not the sender
        if (session_ptr && session_ptr != sender) 
        {
            // Post the deliver task to the session's own strand
            net::post(session_ptr->get_strand(), 
                      [session = session_ptr, message, server_ptr = fmt::ptr(this)]() { // Capture necessary data
                // Check session validity again inside the posted task
                if (session) { 
                    spdlog::trace("[ChatServer {} -> Session {} strand] Delivering broadcast message.", 
                                 server_ptr, fmt::ptr(session.get()));
                    session->deliver(message); 
                }
            });
        } else if (session_ptr == sender) {
             spdlog::trace("[ChatServer {}] Skipping broadcast to sender: {}", fmt::ptr(this), sender->remote_id());
        }
    }
    
    // Log global message to history (if enabled)
    if (history_)
    {
        // Assuming history logging doesn't need to be on the strand, or handled internally by MessageHistory
        history_->log_global_message(message, sender ? sender->nickname() : "system");
    }
}

/**
 * @details 외부에서 `broadcast`를 호출하면, 실제 구현부인 `broadcast_impl`이
 *          `strand_` 위에서 실행되도록 `net::post`를 통해 작업을 예약합니다.
 */
void ChatServer::broadcast(const std::string &message, SessionPtr sender)
{
    if (stopped_)
            return;
    auto self = shared_from_this();
    // Post the implementation to the server's strand
    net::post(strand_, [this, self, message, sender]()
              { 
                  // Check stop condition again inside strand
                  if (stopped_) return;
                  broadcast_impl(message, sender); 
              });
}

/**
 * @details `rooms_mutex_`로 `rooms_` 맵을 보호하며 해당 채팅방을 찾습니다.
 *          찾은 채팅방 객체의 `broadcast` 메서드를 호출하여 방 참여자들에게 메시지를 전달합니다.
 */
bool ChatServer::broadcast_to_room(const std::string &room_name,
                                   const std::string &message,
                                   SessionPtr sender)
{
    if (stopped_)
        return false;
    std::shared_ptr<ChatRoom> room;
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto room_it = rooms_.find(room_name);
        if (room_it == rooms_.end())
        {
            spdlog::error("[ChatServer {}] broadcast_to_room: Room '{}' not found.", fmt::ptr(this), room_name);
            return false;
        }
        room = room_it->second;
    }
    if (room)
    {
        spdlog::debug("Broadcasting to room [{}]: {}", room_name, message);
        room->broadcast(message, sender);
        if (history_)
        {
            history_->log_room_message(room_name, message, sender ? sender->nickname() : "system");
        }
        return true;
    }
    return false;
}

/**
 * @details 비동기적으로 수신자 닉네임을 찾아(`find_session_by_nickname_async`),
 *          수신자가 존재하면 메시지를 `deliver`하고, 송신자에게도 확인 메시지를 보냅니다.
 *          수신자가 없으면 송신자에게 에러 메시지를 보냅니다.
 */
bool ChatServer::send_private_message(const std::string &message,
                                      SessionPtr sender,
                                      const std::string &receiver_nick)
{
    if (stopped_ || !sender || receiver_nick.empty() || message.empty())
        return false;
    auto self = shared_from_this();
    std::string sender_nick = sender->nickname();
    std::string message_copy = message;

    find_session_by_nickname_async(receiver_nick,
                                   [this, self, sender, sender_nick, receiver_nick, message_copy](SessionPtr receiver_session)
                                   {
                                       if (stopped_)
                                           return;
                                       if (receiver_session)
                                       {
                                           std::string formatted_msg = "[PM from " + sender_nick + "]: " + message_copy + "\r\n";
                                           receiver_session->deliver(formatted_msg);
                                           std::string confirmation_msg = "* To " + receiver_nick + ": " + message_copy + "\r\n";
                                           sender->deliver(confirmation_msg);
                                           if (history_)
                                           {
                                               history_->log_private_message(message_copy, sender_nick, receiver_nick);
                                           }
                                           spdlog::info("PM sent from {} to {}", sender_nick, receiver_nick);
                                       }
                                       else
                                       {
                                           std::string error_msg = "Error: 사용자 '" + receiver_nick + "'을(를) 찾을 수 없거나 오프라인 상태입니다.\r\n";
                                           sender->deliver(error_msg);
                                           spdlog::info("PM failed: Receiver {} not found for sender {}", receiver_nick, sender_nick);
                                       }
                                   });
    return true;
}

/**
 * @details 닉네임 유효성을 먼저 검사하고, 통과하면 `try_register_nickname_impl`을
 *          `strand_` 위에서 실행하도록 `net::dispatch`를 통해 예약합니다.
 */
void ChatServer::try_register_nickname_async(const std::string &nickname,
                                             SessionPtr session,
                                             std::function<void(bool)> handler)
{
    if (stopped_)
    {
        net::post(ioc_, [handler]()
                  { handler(false); });
        return;
    }
    if (nickname.empty() || nickname.find_first_of(" \t\n\r\f\v") != std::string::npos ||
        nickname.length() > 20 || nickname == "Server" || nickname == "system")
    {
        spdlog::error("[ChatServer {}] Invalid nickname format attempt (pre-check): '{}'", fmt::ptr(this), nickname);
        net::post(ioc_, [handler]()
                  { handler(false); });
        return;
    }
    std::string nickname_copy = nickname;
    std::weak_ptr<SessionInterface> weak_session = session;
    auto self = shared_from_this();
    net::dispatch(strand_, [this, self, nickname_copy, weak_session, handler = std::move(handler)]() mutable
                  { try_register_nickname_impl(nickname_copy, weak_session, std::move(handler)); });
}

/**
 * @details 이 함수는 반드시 `strand_` 위에서 실행되어야 합니다.
 *          `nicknames_mutex_`로 `nicknames_` 맵을 보호하면서 다음을 수행합니다:
 *          1. 요청된 닉네임이 이미 사용 중인지 확인합니다.
 *             - 사용 중이지만 `weak_ptr`이 만료되었다면, 해당 항목을 제거하고 등록 가능으로 처리합니다.
 *             - 같은 세션이 재요청한 경우 등록 가능으로 처리합니다.
 *          2. 등록이 가능하다면, 이전 닉네임이 있었다면 해당 매핑을 제거합니다.
 *          3. 새로운 닉네임과 세션을 `nicknames_` 맵에 등록합니다.
 *          최종 결과를 콜백 핸들러를 통해 비동기적으로 전달합니다.
 */
void ChatServer::try_register_nickname_impl(const std::string &nickname_copy,
                                            std::weak_ptr<SessionInterface> weak_session,
                                            std::function<void(bool)> handler)
{
    bool success = false;
    SessionPtr session = weak_session.lock();
    if (!session) {
        spdlog::error("[ChatServer {}] Session expired during nickname registration for '{}'", fmt::ptr(this), nickname_copy);
        net::post(ioc_, [handler = std::move(handler)]() { handler(false); });
        return;
    }

    spdlog::debug("[ChatServer {}] try_register_nickname_impl (strand): '{}' for session {}", fmt::ptr(this), nickname_copy, fmt::ptr(session.get()));
    std::string old_nick = session->nickname();
    bool can_register = false;

    // Lock the nicknames_ mutex before accessing the map
    {
        std::lock_guard<std::mutex> lock(nicknames_mutex_); 
        auto it = nicknames_.find(nickname_copy);
        
        if (it == nicknames_.end()) {
            can_register = true;
        } else {
            if (it->second.expired()) {
                spdlog::info("[ChatServer {}] Removing expired nickname entry (strand): '{}'", fmt::ptr(this), nickname_copy);
                nicknames_.erase(it); // Erase requires the lock
                can_register = true;
            } else {
                if (it->second.lock() == session) {
                    can_register = true; // Already registered to this session, allow re-registration (or update)
                } else {
                    spdlog::error("[ChatServer {}] Nickname '{}' already in use by active session (strand).", fmt::ptr(this), nickname_copy);
                    can_register = false;
                }
            }
        }

        if (can_register) {
            // Remove old nickname mapping *if* it belongs to the current session
            if (!old_nick.empty() && old_nick != nickname_copy && old_nick != session->remote_id()) {
                auto old_it = nicknames_.find(old_nick);
                // Check if the old nickname exists and points to the same session
                if (auto locked_old_session = old_it->second.lock(); locked_old_session == session) {
                   nicknames_.erase(old_it); // Erase requires the lock
                   spdlog::info("[ChatServer {}] Removed old nickname '{}' for session {} (strand).", fmt::ptr(this), old_nick, fmt::ptr(session.get()));
                } else if (!locked_old_session) {
                    // Old nickname points to an expired session, remove it anyway
                    nicknames_.erase(old_it);
                    spdlog::info("[ChatServer {}] Removed expired old nickname '{}' during registration (strand).", fmt::ptr(this), old_nick);
                }
            }
            // Register the new nickname
            nicknames_[nickname_copy] = session; // Assignment requires the lock
            success = true;
            spdlog::info("[ChatServer {}] Nickname '{}' registered for session {} (strand).", fmt::ptr(this), nickname_copy, fmt::ptr(session.get()));
        }
    } // Mutex lock scope ends here

    bool final_result = success;
    // Post the result back to the original context (usually io_context)
    net::post(session->get_strand(), [handler = std::move(handler), final_result]() { handler(final_result); });
}

/**
 * @details `nicknames_mutex_`로 보호하면서 `nicknames_` 맵에서 해당 닉네임을 찾아 제거합니다.
 */
void ChatServer::unregister_nickname(const std::string &nickname)
{
    if (nickname.empty())
        return;
    // Lock the mutex before accessing nicknames_
    std::lock_guard<std::mutex> lock(nicknames_mutex_); 
    auto it = nicknames_.find(nickname);
    if (it != nicknames_.end()) {
        // We might want to double-check if the weak_ptr matches the session being removed,
        // but typically unregister is called when the session is already known to be leaving.
        nicknames_.erase(it);
        spdlog::info("[ChatServer {}] Nickname '{}' unregistered (strand).", fmt::ptr(this), nickname);
    }
}

/**
 * @brief 닉네임으로 세션을 비동기적으로 찾습니다.
 * @details `strand_`를 통해 스레드 안전하게 `nicknames_` 맵을 검색합니다.
 *          닉네임에 해당하는 세션을 찾으면 세션에 대한 `shared_ptr`를,
 *          찾지 못하거나 세션이 만료되었으면 `nullptr`를 핸들러에 전달합니다.
 * @param nickname 찾고자 하는 사용자의 닉네임.
 * @param handler 검색 완료 시 `SessionPtr`를 인자로 호출될 콜백 함수.
 */
void ChatServer::find_session_by_nickname_async(const std::string &nickname,
                                                std::function<void(SessionPtr)> handler)
{
    if (stopped_) {
        net::post(ioc_, [handler]() { handler(nullptr); });
        return;
    }
    auto self = shared_from_this();
    net::dispatch(strand_, [this, self, nickname, handler = std::move(handler)]() mutable {
        SessionPtr result = nullptr;
        // Lock the mutex before accessing nicknames_
        {
            std::lock_guard<std::mutex> lock(nicknames_mutex_); 
            auto it = nicknames_.find(nickname);
            if (it != nicknames_.end()) {
                result = it->second.lock(); // lock weak_ptr inside mutex scope
            }
        } // Mutex lock scope ends here
        // Post the result back
        net::post(ioc_, [handler = std::move(handler), result]() {
            handler(result);
        }); 
    });
}

/**
 * @brief 비동기적으로 현재 접속 중인 모든 사용자의 닉네임 목록을 가져옵니다.
 * @details 이 함수는 서버의 `strand`를 통해 스레드 안전하게 `nicknames_` 맵에 접근합니다.
 *          `nicknames_` 맵을 순회하며 만료된 세션(`weak_ptr`가 가리키는 세션이 소멸된 경우)을
 *          자동으로 정리하고, 활성 세션의 닉네임만 수집하여 반환합니다.
 * @param handler 닉네임 목록(`std::vector<std::string>`)을 인자로 받는 콜백 함수.
 *                이 핸들러는 `io_context`의 메인 이벤트 루프에서 실행됩니다.
 */
void ChatServer::get_user_list_async(std::function<void(std::vector<std::string>)> handler)
{
    if (stopped_) {
        net::post(ioc_, [handler]() { handler({}); });
        return;
    }
    auto self = shared_from_this();
    net::dispatch(strand_, [this, self, handler = std::move(handler)]() mutable {
        std::vector<std::string> user_list;
        // Lock the mutex before iterating nicknames_
        {
            std::lock_guard<std::mutex> lock(nicknames_mutex_);
            for (auto it = nicknames_.begin(); it != nicknames_.end(); /* manual increment */) {
                if (auto session = it->second.lock()) { // lock weak_ptr
                    user_list.push_back(it->first);
                    ++it;
                } else {
                    spdlog::info("[ChatServer {}] Removing expired nickname '{}' during user list scan.", fmt::ptr(this), it->first);
                    it = nicknames_.erase(it); // Erase requires lock
                }
            }
        } // Mutex lock scope ends here
        // Post the result back
        net::post(ioc_, [handler = std::move(handler), user_list = std::move(user_list)]() {
            handler(user_list);
        });
    });
}

/**
 * @brief 사용자를 특정 채팅방에 참여시킵니다. (동기 버전)
 */
bool ChatServer::join_room(const std::string& room_name, SessionPtr session)
{
    if (stopped_ || !session) return false;
    
    bool success = false;
    std::string old_room_name = session->current_room();
    std::string nickname = session->nickname();
    std::shared_ptr<ChatRoom> target_room = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        if (!old_room_name.empty() && old_room_name != room_name)
        {
            auto old_room_it = rooms_.find(old_room_name);
            if (old_room_it != rooms_.end())
            {
                auto& old_room = old_room_it->second;
                std::string leave_notice = "* 사용자 '" + nickname + "'님이 '" + old_room_name + "' 방에서 나갔습니다.\r\n";
                old_room->broadcast(leave_notice, session);
                old_room->remove_participant(session);
                spdlog::info("User '{}' removed from old room '{}'", nickname, old_room_name);
                if (old_room->empty())
                {
                    rooms_.erase(old_room_it);
                    spdlog::info("Old room '{}' removed.", old_room_name);
                }
            }
        }
        
        auto room_it = rooms_.find(room_name);
        if (room_it == rooms_.end())
        {
            try
            {
                target_room = std::make_shared<ChatRoom>(room_name);
                rooms_[room_name] = target_room;
                spdlog::info("Created new room: {}", room_name);
            }
            catch (const std::exception& e)
            {
                spdlog::error("Failed to create room '{}': {}", room_name, e.what());
                target_room = nullptr;
            }
        }
        else
        {
            target_room = room_it->second;
        }
        
        if (target_room)
        {
            target_room->add_participant(session);
            session->set_current_room(room_name);
            success = true;
        }
    }
    
    if (success && target_room)
    {
        std::string join_confirm = "* '" + room_name + "' 방에 입장했습니다.\r\n";
        auto members = target_room->sessions();
        join_confirm += "* 현재 멤버 (" + std::to_string(members.size()) + "): ";
        bool first = true;
        size_t valid_member_count = 0;
        std::string member_list_str;
        
        for (const auto& member : members)
        {
            if (member)
            {
                valid_member_count++;
                if (!first)
                    member_list_str += ", ";
                member_list_str += member->nickname() + (member == session ? " (You)" : "");
                first = false;
            }
        }
        join_confirm.replace(join_confirm.find(std::to_string(members.size())), 
                             std::to_string(members.size()).length(), 
                             std::to_string(valid_member_count));
        join_confirm += member_list_str;
        join_confirm += "\r\n";
        session->deliver(join_confirm);
        
        std::string join_notice = "* 사용자 '" + nickname + "'님이 방에 들어왔습니다.\r\n";
        target_room->broadcast(join_notice, session);
        spdlog::info("User '{}' joined room '{}' successfully.", nickname, room_name);
    }
    
    return success;
}

void ChatServer::join_room_async(const std::string& room_name,
                                 SessionPtr session,
                                 std::function<void(bool)> handler)
{
    if (stopped_)
    {
        net::post(ioc_, [handler]() { handler(false); });
        return;
    }
    if (!session || room_name.empty())
    {
        net::post(ioc_, [handler]() { handler(false); });
        return;
    }
    if (room_name.find_first_of(" \t\n\r\f\v") != std::string::npos || room_name.length() > 30)
    {
        spdlog::error("Invalid room name format: '{}'", room_name);
        net::post(ioc_, [handler]() { handler(false); });
        return;
    }
    
    auto self = shared_from_this();
    net::dispatch(strand_, [this, self, room_name, session, handler = std::move(handler)]() mutable {
        bool result = join_room(room_name, session);
        net::post(ioc_, [handler = std::move(handler), result]() {
             handler(result);
        });
    });
}

bool ChatServer::leave_room(const std::string& room_name, SessionPtr session)
{
    if (stopped_ || !session) return false;
    
    bool success = false;
    std::string nickname = session->nickname();
    std::shared_ptr<ChatRoom> room_ptr = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto room_it = rooms_.find(room_name);
        if (room_it != rooms_.end())
        {
            room_ptr = room_it->second;
            std::string leave_notice = "* 사용자 '" + nickname + "'님이 '" + room_name + "' 방에서 나갔습니다.\r\n";
            room_ptr->broadcast(leave_notice, session);
            room_ptr->remove_participant(session);
            spdlog::info("User '{}' left room '{}'.", nickname, room_name);
            if (room_ptr->empty())
            {
                spdlog::info("Room '{}' is empty, removing.", room_name);
                rooms_.erase(room_it);
            }
            success = true;
        }
        else
        {
            spdlog::error("Attempted to leave room '{}' but it was not found.", room_name);
        }
    }
    
    if (success)
    {
        session->set_current_room("");
        session->deliver("* '" + room_name + "' 방에서 퇴장했습니다.\r\n");
    }
    
    return success;
}

void ChatServer::leave_room_async(const std::string& room_name,
                                 SessionPtr session,
                                 std::function<void(bool)> handler)
{
    if (stopped_)
    {
        net::post(ioc_, [handler]() { handler(false); });
        return;
    }
    if (!session || room_name.empty() || session->current_room() != room_name)
    {
        spdlog::error("Leave room request invalid: session null, empty room name, or not in room '{}'", room_name);
        net::post(ioc_, [handler]() { handler(false); });
        return;
    }
    
    auto self = shared_from_this();
    net::dispatch(strand_, [this, self, room_name, session, handler = std::move(handler)]() mutable {
        bool result = leave_room(room_name, session);
        net::post(ioc_, [handler = std::move(handler), result]() {
             handler(result);
        });
    });
}

void ChatServer::leave_all_rooms_impl(const SessionPtr& session)
{
    if (!session) return;
    
    std::string current_room = session->current_room();
    if (!current_room.empty())
    {
        leave_room(current_room, session);
    }
}

bool ChatServer::load_config()
{
    spdlog::info("[Server {}] load_config() (Placeholder)", fmt::ptr(this));
    return true;
}

bool ChatServer::save_config()
{
    spdlog::info("[Server {}] save_config() (Placeholder)", fmt::ptr(this));
    return true;
}

bool ChatServer::load_users()
{
    spdlog::info("[Server {}] load_users() (Placeholder)", fmt::ptr(this));
    return true;
}

bool ChatServer::save_users()
{
    spdlog::info("[Server {}] save_users() (Placeholder)", fmt::ptr(this));
    return true;
}

void ChatServer::set_history_enabled(bool enable)
{
    if (history_)
        history_->set_enabled(enable);
}

bool ChatServer::is_history_enabled() const 
{ 
    return history_ ? history_->is_enabled() : false; 
}

std::vector<std::string> ChatServer::load_global_history(size_t limit)
{
    return history_ ? history_->load_global_history(limit) : std::vector<std::string>();
}

std::vector<std::string> ChatServer::load_private_history(const std::string &u1, const std::string &u2, size_t limit)
{
    return history_ ? history_->load_private_history(u1, u2, limit) : std::vector<std::string>();
}

std::vector<std::string> ChatServer::load_room_history(const std::string &room, size_t limit)
{
    return history_ ? history_->load_room_history(room, limit) : std::vector<std::string>();
}

std::string ChatServer::hash_password(const std::string &password)
{
    spdlog::info("hash_password (Placeholder - DO NOT USE IN PRODUCTION)");
    return "hashed_" + password;
}

void ChatServer::system_broadcast(const std::string &message) 
{ 
    broadcast(message, nullptr); 
}

UserAccount::UserAccount(const std::string &username, const std::string &password_hash, bool is_admin)
    : username_(username), password_hash_(password_hash), is_admin_(is_admin) {}

bool UserAccount::check_password(const std::string &ph) const 
{ 
    return password_hash_ == ph; 
}

void UserAccount::set_password(const std::string &nph) 
{ 
    password_hash_ = nph; 
}

void UserAccount::set_admin(bool ia) 
{ 
    is_admin_ = ia; 
}

void UserAccount::update_login_info(const std::string &ip, const std::string &time)
{
    last_ip_ = ip;
    last_login_ = time;
}

FileTransferInfo::FileTransferInfo(const std::string &id, const std::string &filename, size_t filesize,
                                   std::shared_ptr<SessionInterface> sender, std::shared_ptr<SessionInterface> receiver)
    : id_(id), filename_(filename), filesize_(filesize), sender_(sender), receiver_(receiver) {}