/**
 * @file ChatSession.cpp
 * @brief `ChatSession` 클래스의 구현부입니다.
 * @details 이 파일은 `ChatSession.hpp`에 선언된 멤버 함수들을 실제로 정의합니다.
 */

#include "ChatSession.hpp" // 해당 클래스의 헤더를 가장 먼저 인클루드합니다.

#include "ChatServer.hpp"  // ChatServer 클래스 정의 인클루드
#include "spdlog/spdlog.h" // 로깅 라이브러리 spdlog 인클루드

// 필요한 Boost.Asio 헤더들
#include <boost/asio/bind_executor.hpp> // 비동기 작업 핸들러를 특정 executor(strand)에 바인딩
#include <boost/asio/buffer.hpp>        // 데이터 버퍼 관리를 위함
#include <boost/asio/dispatch.hpp>      // 핸들러를 executor 컨텍스트 내에서 즉시 실행
#include <boost/asio/read_until.hpp>    // 특정 구분자까지 데이터를 읽는 비동기 작업
#include <boost/asio/write.hpp>         // 데이터를 쓰는 비동기 작업

// 표준 라이브러리 헤더들
#include <atomic> // 원자적 연산을 위함 (std::atomic)
#include <deque>  // 양방향 큐 (전송 메시지 큐로 사용)
#include <memory> // 스마트 포인터 (std::shared_ptr, std::enable_shared_from_this)
#include <string>
#include <vector>
#include <fmt/format.h> // 문자열 포맷팅 라이브러리

namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;

//------------------------------------------------------------------------------
// ChatSession 클래스 멤버 함수 구현
//------------------------------------------------------------------------------

/**
 * @details 소켓과 서버 포인터를 멤버 변수에 저장하고, 스트랜드를 초기화합니다.
 *          클라이언트의 원격 엔드포인트 정보(IP:PORT)를 `remote_id_`로,
 *          초기 `nickname_`으로 설정합니다.
 */
ChatSession::ChatSession(tcp::socket socket, std::shared_ptr<ChatServer> server)
    : socket_(std::move(socket)), server_(server), strand_(net::make_strand(socket_.get_executor())), stopped_(false), writing_flag_(false)
{
    try {
        remote_id_ = socket_.remote_endpoint().address().to_string() + ":" +
                     std::to_string(socket_.remote_endpoint().port());
        spdlog::info("[ChatSession {} - {}] Created.", static_cast<void*>(this), remote_id_);
    } catch (const std::exception& e) {
        spdlog::error("[ChatSession {} - ???] Failed to get remote endpoint: {}", static_cast<void*>(this), e.what());
        remote_id_ = "UnknownClient";
    }
    nickname_ = remote_id_; // Default nickname
}

/**
 * @details 세션이 소멸될 때 로그를 남깁니다. 닉네임 해제는 `ChatServer::leave`에서 처리됩니다.
 */
ChatSession::~ChatSession() {
    spdlog::info("[ChatSession {} - {}] Destroyed.", static_cast<void*>(this), remote_id_);
    // Nickname unregistration is handled by ChatServer::leave
}

/**
 * @details 세션이 이미 중지된 경우 아무 작업도 하지 않습니다.
 *          `ChatServer`에 자신을 참여자로 등록(`join`)하고,
 *          클라이언트에게 환영 메시지들을 전송한 후,
 *          첫 비동기 읽기 작업(`do_read`)을 시작합니다.
 */
void ChatSession::start()
{
    if (stopped_.load())
    {
        spdlog::warn("[ChatSession {}] start() called on stopped session.", static_cast<void*>(this));
        return;
    }

    spdlog::info("[ChatSession {}] Starting session for {}.", static_cast<void*>(this), remote_id_);

    // Register with the server first. This is asynchronous.
    server_->join(shared_from_this());

    // Use deliver() to send welcome messages asynchronously via the queue and strand
    deliver("Welcome to the CherryRecorder Chat Server!\r\n");
    deliver(fmt::format("Your temporary ID is: {}\r\n", remote_id_));
    deliver("Please set your nickname using /nick <nickname>\r\n");
    deliver("Enter /help for a list of commands.\r\n");
    deliver("Enter /join <roomname> to join or create a room.\r\n");
    // Remove initial join notification - will be sent when nickname is set

    // Start reading asynchronously
    net::dispatch(strand_, [self = shared_from_this_chat()]() {
        if (!self->stopped_.load()) {
            self->do_read();
        }
    });
}

/**
 * @details `stopped_` 플래그를 원자적으로 true로 설정하여 중복 실행을 방지합니다.
 *          스트랜드를 통해 소켓에 대한 모든 비동기 작업을 취소하고,
 *          소켓을 정상 종료(shutdown)한 후 닫습니다.
 *          마지막으로 `ChatServer`에게 `leave`를 호출하여 세션 제거를 요청합니다.
 */
void ChatSession::stop_session() {
    if (stopped_.exchange(true)) return; // Ensure stop logic runs only once

    auto self = shared_from_this(); // Keep alive while posting
    // Use dispatch to run on the strand safely
    net::dispatch(strand_, [this, self]() {
        if (socket_.is_open()) {
            boost::system::error_code ignored_ec;
            
            // Cancel any pending asynchronous operations (read, write)
            socket_.cancel(ignored_ec);
            if (ignored_ec) {
                spdlog::warn("[ChatSession {} - {}] Socket cancel error: {}", 
                             static_cast<void*>(this), remote_id_, ignored_ec.message());
            }

            // Gracefully shutdown the socket
            socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);

            // Close the socket
            socket_.close(ignored_ec);
            if (ignored_ec) {
                 spdlog::error("[ChatSession {} - {}] Socket close error: {}", 
                              static_cast<void*>(this), remote_id_, ignored_ec.message());
            } else {
                 spdlog::info("[ChatSession {} - {}] Socket closed successfully.", static_cast<void*>(this), remote_id_);
            }
        }
        // Notify the server to remove this session.
        // Assuming server_ is a shared_ptr as per compile error.
        if(server_) { // Directly check the shared_ptr
             server_->leave(self);
        } else {
             // This case might be less likely if server_ is a shared_ptr initialized in constructor,
             // but keep warning for safety.
             spdlog::warn("[ChatSession {} - {}] Server pointer is null when calling leave.", static_cast<void*>(this), remote_id_);
        }
    });
}

/**
 * @details 메시지를 `write_msgs_` 큐에 추가합니다. `net::post`를 사용하여
 *          이 작업을 세션의 스트랜드에서 안전하게 실행하도록 합니다.
 *          만약 쓰기 작업이 진행 중이지 않았다면(`writing_flag_`가 false이고 큐가 비어있었다면),
 *          새로운 쓰기 작업(`do_write_strand`)을 시작하여 큐의 메시지를 전송합니다.
 */
void ChatSession::deliver(const std::string& msg) {
    auto self = shared_from_this();
    net::post(strand_, [this, self, msg]() {
        if (stopped_) {
            spdlog::trace("[ChatSession {}] deliver called but stopped, ignoring msg: {}", static_cast<void*>(this), msg);
            return;
        }

        // Check writing_flag_ BEFORE pushing to the queue to decide if we need to start the write loop.
        // This avoids a potential race condition if on_write completes between the check and the push.
        bool start_write = write_msgs_.empty() && !writing_flag_;
        
        write_msgs_.push_back(msg + "\r\n"); // Always add message to queue
        spdlog::trace("[ChatSession {}] Queued msg ({}): '{}'. Queue size: {}. Start write? {}", 
                     static_cast<void*>(this), msg.length(), msg, write_msgs_.size(), start_write);

        // Only start the write loop if it wasn't already running AND the queue was previously empty.
        // Relying solely on writing_flag_ might miss starting the loop if deliver is called
        // exactly when on_write finishes but before the next do_write_strand is called from on_write.
        // Checking both empty queue and not writing seems safer.
        if (start_write) {
            do_write_strand();
        }
    });
}

// --- Accessors ---
const std::string& ChatSession::nickname() const { return nickname_; }
const std::string& ChatSession::remote_id() const { return remote_id_; }
const std::string& ChatSession::current_room() const { return current_room_; }
void ChatSession::set_current_room(const std::string& room_name) {
    // Maybe add validation or logging here if needed
    current_room_ = room_name;
}

// --- Private Methods ---

/**
 * @details 스트림에서 명령어와 인자를 파싱합니다.
 *          - `/nick`: 닉네임 변경을 시도합니다. 유효성 검사를 거친 후 `ChatServer`에 비동기적으로 닉네임 등록을 요청합니다.
 *          - `/join`: 채팅방 참여를 시도합니다. `ChatServer`에 비동기적으로 방 참여를 요청합니다.
 *          - `/leave`: 현재 채팅방에서 나갑니다.
 *          - `/users`: 현재 접속 중인 모든 사용자 목록을 요청합니다.
 *          - `/quit`: 세션을 종료합니다.
 *          - `/help`: 사용 가능한 명령어 목록을 보여줍니다.
 *          명령어가 아닌 경우, 일반 채팅 메시지로 간주하고 현재 방 또는 전체에 브로드캐스트합니다.
 */
void ChatSession::process_command(const std::string& command_line) {
    if (stopped_) return;
    std::vector<std::string> responses; // For synchronous error/info messages
    std::istringstream iss(command_line);
    std::string cmd;
    iss >> cmd;
    std::string arg1, remaining_args;
    if (iss >> arg1) {
        std::getline(iss, remaining_args);
        // Remove leading space from remaining args if present
        if (!remaining_args.empty() && remaining_args.front() == ' ') {
             remaining_args.erase(0, 1);
        }
    }
    std::string full_arg = arg1;
    if (!remaining_args.empty()) {
        full_arg += " " + remaining_args; // Reconstruct full argument
    }

    // Command handlers
    if (cmd == "/nick") {
        const std::string& requested_nick = full_arg;
        // Validation...
        if (requested_nick.empty()) responses.push_back("Error: 닉네임은 비어있을 수 없습니다.\r\n");
        else if (requested_nick.find_first_of(" \t\n\r\f\v") != std::string::npos) responses.push_back("Error: 닉네임에 공백 문자를 포함할 수 없습니다.\r\n");
        else if (requested_nick.length() > 20) responses.push_back("Error: 닉네임은 20자를 초과할 수 없습니다.\r\n");
        else {
            std::string old_nick = nickname_;
            std::string nick_copy = requested_nick;
            spdlog::info("[ChatSession {}] Attempting nick change: {} -> {}", static_cast<void*>(this), old_nick, nick_copy);
            auto self = shared_from_this();
            if (server_) { // Check server validity
                server_->try_register_nickname_async(nick_copy, self,
                    [this, self, old_nick, nick_copy](bool success) {
                        // This lambda runs asynchronously
                        if (stopped_) return; // Check if stopped during callback
                        std::vector<std::string> async_responses;
                        if (success) {
                            std::string previous_nick = nickname_; // Store before update
                            nickname_ = nick_copy; // Update local nickname
                            spdlog::info("[ChatSession {}] Nickname change success: {} -> {}", static_cast<void*>(this), previous_nick, nick_copy);
                            async_responses.push_back("* 닉네임이 '" + nick_copy + "'(으)로 변경되었습니다.\r\n");
                            
                            // Check if this is the first nickname change (from IP:PORT to actual nickname)
                            bool is_first_nickname = (previous_nick == remote_id_);
                            
                            if (is_first_nickname) {
                                // Broadcast join message for first-time nickname setting
                                std::string join_notice = "* 사용자 '" + nick_copy + "'님이 입장했습니다.\r\n";
                                if(server_) server_->broadcast(join_notice, self); // Exclude self
                            } else {
                                // Broadcast nickname change globally
                                std::string notice = "* 사용자 '" + previous_nick + "'의 닉네임이 '" + nick_copy + "'(으)로 변경되었습니다.\r\n";
                                if(server_) server_->broadcast(notice, self); // Exclude self
                            }
                        } else {
                            spdlog::info("[ChatSession {}] Nickname change failed: {}", static_cast<void*>(this), nick_copy);
                            async_responses.push_back("Error: 닉네임 '" + nick_copy + "'은(는) 이미 사용 중이거나 유효하지 않습니다.\r\n");
                        }
                        // Deliver responses generated in the callback
                        for(const auto& resp : async_responses) deliver(resp);
                    });
            } else {
                 spdlog::error("[ChatSession {}] Server pointer is null in process_command(\"/nick\")", static_cast<void*>(this));
                 responses.push_back("Error: 서버 내부 오류로 닉네임 변경 불가.\r\n");
            }
            // Return immediately after starting async operation or adding sync error
            // Synchronous responses (like validation errors) will be sent below
            // Asynchronous responses are sent via deliver in the callback
        }
    } else if (cmd == "/join") {
        const std::string& room_name = full_arg;
        // Validation...
        if (room_name.empty()) responses.push_back("Error: 방 이름은 비어있을 수 없습니다.\r\n");
        else if (room_name.find_first_of(" \t\n\r\f\v") != std::string::npos) responses.push_back("Error: 방 이름에 공백 문자를 포함할 수 없습니다.\r\n");
        else if (room_name.length() > 30) responses.push_back("Error: 방 이름은 30자를 초과할 수 없습니다.\r\n");
        else if (room_name == current_room_) responses.push_back("* 이미 '" + room_name + "' 방에 있습니다.\r\n");
        else {
            auto self = shared_from_this();
            if (server_) {
                server_->join_room_async(room_name, self,
                    [this, self, room_name](bool success) {
                         if (stopped_) return;
                         std::vector<std::string> async_responses;
                         if (success) {
                             spdlog::info("[ChatSession {}] Joined room '{}' successfully", static_cast<void*>(this), room_name);
                             // Confirmation messages are sent by join_room_impl
                         } else {
                             spdlog::info("[ChatSession {}] Failed to join room '{}'", static_cast<void*>(this), room_name);
                             async_responses.push_back("Error: '" + room_name + "' 방 입장에 실패했습니다.\r\n");
                         }
                         for(const auto& resp : async_responses) deliver(resp);
                    });
            } else {
                 spdlog::error("[ChatSession {}] Server pointer is null in process_command(\"/join\")", static_cast<void*>(this));
                 responses.push_back("Error: 서버 내부 오류로 방 입장 불가.\r\n");
            }
        }
    } else if (cmd == "/leave") {
        if (current_room_.empty()) responses.push_back("Error: 현재 어떤 방에도 없습니다.\r\n");
        else {
            std::string room_to_leave = current_room_;
            auto self = shared_from_this();
             if (server_) {
                server_->leave_room_async(room_to_leave, self,
                    [this, self, room_to_leave](bool success) {
                         if (stopped_) return;
                         std::vector<std::string> async_responses;
                         if (success) {
                             spdlog::info("[ChatSession {}] Left room '{}' successfully", static_cast<void*>(this), room_to_leave);
                             // Confirmation sent by leave_room_impl
                         } else {
                             spdlog::info("[ChatSession {}] Failed to leave room '{}'", static_cast<void*>(this), room_to_leave);
                             async_responses.push_back("Error: '" + room_to_leave + "' 방 퇴장에 실패했습니다.\r\n");
                         }
                         for(const auto& resp : async_responses) deliver(resp);
                    });
            } else {
                 spdlog::error("[ChatSession {}] Server pointer is null in process_command(\"/leave\")", static_cast<void*>(this));
                 responses.push_back("Error: 서버 내부 오류로 방 퇴장 불가.\r\n");
            }
        }
    } else if (cmd == "/users") {
        auto self = shared_from_this();
         if (server_) {
            server_->get_user_list_async(
                [this, self](std::vector<std::string> users) {
                    if (stopped_) return;
                    std::vector<std::string> async_responses;
                    async_responses.push_back("* 현재 접속 중인 사용자 (" + std::to_string(users.size()) + "):\r\n");
                    for (const auto& user : users) {
                        async_responses.push_back("  - " + user + (user == nickname_ ? " (You)" : "") + "\r\n");
                    }
                    for(const auto& resp : async_responses) deliver(resp);
                });
        } else {
             spdlog::error("[ChatSession {}] Server pointer is null in process_command(\"/users\")", static_cast<void*>(this));
             responses.push_back("Error: 서버 내부 오류로 사용자 목록 조회 불가.\r\n");
        }
    } else if (cmd == "/quit") {
        deliver("* 연결을 종료합니다...\r\n"); // Send disconnect message first
        stop_session(); // Then initiate session stop
        return; // Don't send other responses
    } else if (cmd == "/help") {
        responses.push_back("--- 도움말 ---\r\n");
        responses.push_back("/nick <닉네임> - 닉네임 변경\r\n");
        responses.push_back("/join <방이름> - 방 입장/생성\r\n");
        responses.push_back("/leave - 현재 방 퇴장\r\n");
        responses.push_back("/users - 현재 접속자 목록 보기\r\n");
        responses.push_back("/quit - 채팅 종료\r\n");
        responses.push_back("/help - 도움말 표시\r\n");
        responses.push_back("-------------\r\n");
    } else if (cmd.empty()) {
        // Ignore empty line
    } else if (cmd[0] == '/') {
        responses.push_back("Error: 알 수 없는 명령어 '" + cmd + "'. '/help'를 입력하여 도움말을 확인하세요.\r\n");
    } else {
        // Treat as a chat message
        std::string message_content = command_line; // Use the whole line as message
        if (server_) {
            auto self = shared_from_this(); // ★★★ Get shared_ptr to self ★★★
            if (!current_room_.empty()) {
                std::string formatted_message = "[" + nickname_ + " @ " + current_room_ + "]: " + message_content + "\r\n";
                // ★★★ Pass 'self' as the sender ★★★
                server_->broadcast_to_room(current_room_, formatted_message, self);
            } else {
                std::string formatted_message = "[" + nickname_ + "]: " + message_content + "\r\n";
                // ★★★ Pass 'self' as the sender ★★★
                server_->broadcast(formatted_message, self);
            }
        } else {
             spdlog::error("[ChatSession {}] Server pointer is null when processing message", static_cast<void*>(this));
             responses.push_back("Error: 메시지를 전송할 수 없습니다 (서버 오류).\r\n");
        }
    }

    // Deliver any synchronous responses generated (e.g., validation errors)
    for (const auto& response : responses) {
        deliver(response);
    }
}

/**
 * @details `net::async_read_until`을 사용하여 소켓으로부터 데이터를 비동기적으로 읽습니다.
 *          데이터는 `read_buffer_`에 저장되며, 개행 문자 `\n`를 만날 때까지 읽습니다.
 *          읽기 작업 완료 시 `on_read` 핸들러가 호출됩니다. 핸들러는 스트랜드 위에서 실행됩니다.
 */
void ChatSession::do_read() {
    if (stopped_ || !socket_.is_open()) return;
    auto self = shared_from_this();
    // Read until newline character
    net::async_read_until(socket_, read_buffer_, "\n",
        // Ensure handler runs on the strand
        boost::asio::bind_executor(strand_,
            [this, self](beast::error_code ec, std::size_t length) {
                on_read(ec, length);
            }
        )
    );
}

/**
 * @details 읽기 작업의 결과를 처리합니다.
 *          - 에러가 없는 경우: 버퍼에서 한 줄씩 메시지를 추출하여 `process_command`로 전달합니다.
 *            버퍼에 데이터가 남아있을 수 있으므로, 모든 줄을 처리한 후 다시 `do_read`를 호출하여 다음 메시지를 기다립니다.
 *          - 에러가 발생한 경우: EOF(연결 종료)나 연결 리셋 등 일반적인 종료 상황은 정보 로그를,
 *            그 외의 에러는 에러 로그를 남기고 `stop_session`을 호출하여 세션을 종료합니다.
 */
void ChatSession::on_read(beast::error_code ec, std::size_t length) {
    // Check if stopped or socket closed before processing
    if (stopped_ || !socket_.is_open()) return;

    if (!ec) {
        try {
            std::istream is(std::addressof(read_buffer_));
            std::string message;
            // Process all complete lines in the buffer
            while (std::getline(is, message)) {
                // Handle potential CRLF by removing trailing CR if present
                if (!message.empty() && message.back() == '\r') {
                    message.pop_back();
                }

                if (!message.empty()) {
                    spdlog::info("[ChatSession {} - {}] Received: {}", static_cast<void*>(this), remote_id_, message);
                    // Process the received line as a command or message
                    process_command(message);
                } else {
                    // Log receipt of an empty line if needed
                    spdlog::info("[ChatSession {} - {}] Received empty line.", static_cast<void*>(this), remote_id_);
                }
            }
             // Clear EOF state if getline stopped because it reached the end of the buffer
             // without finding a newline, allowing subsequent reads to work correctly.
             is.clear();

             // Continue reading if the session is still active
             if (!stopped_ && socket_.is_open()) {
                do_read();
             }

        } catch (const std::exception& e) {
             spdlog::error("[ChatSession {} - {}] Exception during read processing: {}", static_cast<void*>(this), remote_id_, e.what());
             stop_session(); // Stop session on processing error
        }
    } else {
        // Handle read errors (EOF, connection reset, aborted operation, etc.)
        if (ec != net::error::eof && ec != net::error::connection_reset && ec != net::error::operation_aborted) {
            spdlog::error("[ChatSession {} - {}] Read error: {} ({})", static_cast<void*>(this), remote_id_, ec.message(), ec.value());
        } else if (ec == net::error::eof) {
             spdlog::info("[ChatSession {} - {}] Connection closed by peer (EOF).", static_cast<void*>(this), remote_id_);
        } else if (ec == net::error::connection_reset) {
             spdlog::info("[ChatSession {} - {}] Connection reset by peer.", static_cast<void*>(this), remote_id_);
        } else { // operation_aborted
             // This is expected during shutdown, so usually INFO level is fine
             spdlog::info("[ChatSession {} - {}] Read operation aborted.", static_cast<void*>(this), remote_id_);
        }
        stop_session(); // Initiate session cleanup on any read error or disconnect
    }
}

/**
 * @details 이 함수는 반드시 스트랜드 내에서 호출되어야 합니다.
 *          `writing_flag_`를 true로 설정하여 다른 쓰기 작업이 동시에 시작되지 않도록 하고,
 *          `net::async_write`를 호출하여 `write_msgs_` 큐의 첫 번째 메시지를 클라이언트에게 전송합니다.
 *          쓰기 작업 완료 시 `on_write` 핸들러가 호출됩니다.
 */
void ChatSession::do_write_strand() {
    // Pre-conditions checked before calling: must be on strand, not stopped, queue not empty, not writing
    if (stopped_ || write_msgs_.empty() || writing_flag_) {
        // This state should ideally not be reached if called correctly from deliver/on_write
        spdlog::warn("[ChatSession {}] do_write_strand called in unexpected state (stopped={}, queue_empty={}, writing={})",
                     static_cast<void*>(this), stopped_.load(), write_msgs_.empty(), writing_flag_);
        return; 
    }

    writing_flag_ = true; // Mark that a write operation is now in progress
    spdlog::trace("[ChatSession {}] Starting async_write for msg: '{}'", static_cast<void*>(this), write_msgs_.front());

    auto self = shared_from_this(); 
    net::async_write(socket_,
        net::buffer(write_msgs_.front()), 
        boost::asio::bind_executor(strand_,
            [this, self](std::error_code ec, std::size_t length) {
                // Log completion regardless of error code
                spdlog::trace("[ChatSession {}] async_write completed. Error: '{}' ({}), Bytes: {}", 
                             static_cast<void*>(this), ec.message(), ec.value(), length);
                on_write(ec, length);
            }
        )
    );
}

/**
 * @details 비동기 쓰기 작업의 결과를 처리합니다.
 *          - 에러가 없는 경우: 성공적으로 전송된 메시지를 `write_msgs_` 큐에서 제거합니다.
 *          - 에러가 발생한 경우: 에러 로그를 남기고 세션을 종료합니다.
 *          `writing_flag_`를 false로 리셋한 후, 만약 큐에 아직 전송할 메시지가 남아있다면 `do_write_strand`를
 *          다시 호출하여 다음 메시지를 전송합니다. 이를 통해 메시지가 순서대로 계속 전송됩니다.
 */
void ChatSession::on_write(std::error_code ec, std::size_t /*length*/) {
    // Already on strand via bind_executor
    if (stopped_) {
        spdlog::trace("[ChatSession {}] on_write called but stopped.", static_cast<void*>(this));
        // Ensure writing_flag is reset if we were writing when stop occurred
        writing_flag_ = false; 
        return;
    }
    
    // Important: writing_flag_ should be true here if do_write_strand was called correctly
    if (!writing_flag_) {
        spdlog::error("[ChatSession {}] on_write called but writing_flag_ was false! Error: {}", static_cast<void*>(this), ec.message());
        // Attempt to recover or log, but this indicates a logic error
    }

    bool message_written_successfully = false;
    if (!ec) {
        // Write successful, remove the message from the queue
        if (!write_msgs_.empty()) { // Safety check
            spdlog::trace("[ChatSession {}] Write success, removing msg: '{}'", static_cast<void*>(this), write_msgs_.front());
            write_msgs_.pop_front();
            message_written_successfully = true;
        } else {
             spdlog::error("[ChatSession {}] on_write success, but write queue was empty!", static_cast<void*>(this));
        }
    } else {
        spdlog::error("[ChatSession {}] Write error: {} ({})", static_cast<void*>(this), remote_id_, ec.message(), ec.value());
        write_msgs_.clear(); // Clear the queue on error
         // Use the fully qualified error code constant for comparison
         if (ec.value() != boost::asio::error::operation_aborted) {
            net::post(strand_, [self = shared_from_this()]() { self->stop_session(); });
         }
    }

    // Crucial: Reset the flag AFTER processing the result and BEFORE starting the next write.
    writing_flag_ = false; 

    // If the last write was successful AND more messages are in the queue AND the session is active,
    // start the next write.
    if (message_written_successfully && !write_msgs_.empty() && !stopped_ && socket_.is_open()) {
        spdlog::trace("[ChatSession {}] More messages in queue ({}), starting next write.", static_cast<void*>(this), write_msgs_.size());
        do_write_strand(); 
    } else {
         spdlog::trace("[ChatSession {}] No more messages to write or stopped/closed.", static_cast<void*>(this));
    }
}