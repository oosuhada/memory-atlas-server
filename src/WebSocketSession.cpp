/**
 * @file WebSocketSession.cpp
 * @brief `WebSocketSession` 클래스의 멤버 함수 구현부입니다.
 */
#include "WebSocketSession.hpp"
#include "ChatServer.hpp"
#include <spdlog/spdlog.h>
#include <boost/beast/core/buffers_to_string.hpp>

/**
 * @details TCP 소켓의 소유권을 WebSocket 스트림으로 이동시키고,
 *          서버 포인터와 스트랜드를 초기화합니다.
 *          클라이언트의 원격 엔드포인트(IP:PORT)를 가져와 `remote_id_`와 초기 `nickname_`으로 설정합니다.
 */
WebSocketSession::WebSocketSession(tcp::socket&& socket, std::shared_ptr<ChatServer> server)
    : ws_(std::move(socket))
    , server_(server)
    , strand_(net::make_strand(ws_.get_executor()))
{
    // Remote endpoint 정보를 저장
    try {
        auto endpoint = ws_.next_layer().socket().remote_endpoint();
        remote_id_ = endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
        nickname_ = remote_id_; // 초기 닉네임은 remote_id로 설정
    } catch (const std::exception& e) {
        spdlog::error("Failed to get remote endpoint: {}", e.what());
        remote_id_ = "unknown";
        nickname_ = "unknown";
    }
}

WebSocketSession::~WebSocketSession()
{
    spdlog::info("[WebSocketSession {}] Destructor called", remote_id_);
}

/**
 * @details `ws_.async_accept`를 호출하여 WebSocket 핸드셰이크를 비동기적으로 시작합니다.
 *          핸드셰이크가 완료되면 `on_accept` 콜백이 호출됩니다.
 */
void WebSocketSession::run()
{
    // WebSocket 핸드셰이크를 수락
    ws_.async_accept(
        beast::bind_front_handler(
            &WebSocketSession::on_accept,
            std::enable_shared_from_this<WebSocketSession>::shared_from_this()));
}

/**
 * @details 핸드셰이크 성공 시, WebSocket 타임아웃 등 옵션을 설정하고
 *          `ChatServer`에 세션을 등록(`join`)합니다.
 *          클라이언트에게 환영 메시지를 전송한 후, 첫 비동기 읽기(`do_read`)를 시작합니다.
 *          실패 시 에러 로그를 남기고 세션을 종료합니다.
 */
void WebSocketSession::on_accept(beast::error_code ec)
{
    if(ec)
    {
        // This error is expected when the health checker disconnects.
        // Log it as info to avoid spamming the error logs.
        spdlog::info("[WebSocketSession {}] Accept failed: {}", remote_id_, ec.message());
        return;
    }

    // Read a message
    do_read();
    
    spdlog::info("[WebSocketSession {}] WebSocket connection accepted", remote_id_);
    
    // WebSocket 옵션 설정
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res)
        {
            res.set(beast::http::field::server, "CherryRecorder/1.0");
        }));
    ws_.read_message_max(max_message_size_); // 최대 메시지 크기 설정
    
    // 서버에 세션 등록
    if (server_) {
        server_->join(shared_from_this());
    }
    
    // 환영 메시지 전송
    deliver("* CherryRecorder 채팅 서버에 연결되었습니다.\r\n");
    deliver("* /nick <닉네임> - 닉네임 변경\r\n");
    deliver("* /pm <닉네임> <메시지> - 개인 메시지\r\n");
    deliver("* /list - 접속자 목록\r\n");
}

/**
 * @details `ws_.async_read`를 호출하여 클라이언트로부터 메시지를 비동기적으로 읽습니다.
 *          메시지 수신이 완료되면 `on_read` 콜백이 호출됩니다.
 */
void WebSocketSession::do_read()
{
    // 비동기 읽기 작업 시작
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &WebSocketSession::on_read,
            std::enable_shared_from_this<WebSocketSession>::shared_from_this()));
}

/**
 * @details 읽기 작업의 결과를 처리합니다.
 *          - `websocket::error::closed`: 클라이언트가 정상적으로 연결을 닫은 경우로, 서버에서도 세션을 정리합니다.
 *          - 기타 에러: 에러 로그를 남기고 세션을 정리합니다.
 *          - 성공: 수신된 데이터를 문자열로 변환하고 버퍼를 비운 후, `process_message`를 호출하여 메시지를 처리합니다.
 *            그 다음, `do_read`를 다시 호출하여 다음 메시지를 기다립니다.
 */
void WebSocketSession::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);
    
    if (ec == websocket::error::closed) {
        // 연결이 정상적으로 종료됨
        spdlog::info("[WebSocketSession {}] Connection closed", remote_id_);
        if (server_) {
            server_->leave(shared_from_this());
        }
        return;
    }
    
    if (ec) {
        spdlog::error("[WebSocketSession {}] Read failed: {}", remote_id_, ec.message());
        if (server_) {
            server_->leave(shared_from_this());
        }
        return;
    }
    
    // 수신한 메시지 처리
    std::string message = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    
    // 메시지 처리
    process_message(message);
    
    // 다음 메시지 읽기
    do_read();
}

/**
 * @details 수신된 메시지를 파싱하여 명령어와 일반 메시지를 구분하여 처리합니다.
 *          - `/nick`, `/pm`, `/list`, `/join`, `/leave` 등 다양한 명령어를 처리합니다.
 *          - 명령어 처리는 대부분 `ChatServer`의 해당 비동기 함수를 호출하여 위임합니다.
 *          - 명령어가 아닌 경우 일반 채팅 메시지로 간주하고, 현재 방 또는 전체에 브로드캐스트합니다.
 */
void WebSocketSession::process_message(const std::string& message)
{
    if (message.empty()) {
        return;
    }
    
    spdlog::info("[WebSocketSession {}] Received: {}", nickname_, message);
    
    // 명령어 처리
    if (message[0] == '/') {
        std::istringstream iss(message);
        std::string command;
        iss >> command;
        
        if (command == "/nick") {
            std::string new_nick;
            iss >> new_nick;
            if (!new_nick.empty()) {
                server_->try_register_nickname_async(new_nick, shared_from_this(),
                    [this, new_nick](bool success) {
                        if (success) {
                            std::string old_nick = nickname_;
                            set_nickname(new_nick);
                            deliver("* 닉네임이 '" + new_nick + "'(으)로 변경되었습니다.\r\n");
                            
                            // Check if this is the first nickname change (from IP:PORT to actual nickname)
                            bool is_first_nickname = (old_nick == remote_id_);
                            
                            // 다른 사용자들에게 알림
                            if (server_) {
                                if (is_first_nickname) {
                                    // Broadcast join message for first-time nickname setting
                                    std::string join_msg = "* 사용자 '" + new_nick + "'님이 입장했습니다.\r\n";
                                    server_->broadcast(join_msg, shared_from_this());
                                } else {
                                    // Broadcast nickname change message
                                    std::string notify_msg = "* '" + old_nick + "'님이 '" + new_nick + "'(으)로 닉네임을 변경했습니다.\r\n";
                                    server_->broadcast(notify_msg, shared_from_this());
                                }
                            }
                        } else {
                            deliver("Error: 닉네임 '" + new_nick + "'은(는) 이미 사용 중입니다.\r\n");
                        }
                    });
            } else {
                deliver("Error: 사용법: /nick <닉네임>\r\n");
            }
        }
        else if (command == "/pm") {
            std::string target_nick;
            iss >> target_nick;
            
            std::string pm_message;
            std::getline(iss, pm_message);
            pm_message.erase(0, pm_message.find_first_not_of(" \t"));
            
            if (!target_nick.empty() && !pm_message.empty()) {
                server_->send_private_message(pm_message, shared_from_this(), target_nick);
            } else {
                deliver("Error: 사용법: /pm <닉네임> <메시지>\r\n");
            }
        }
        else if (command == "/list") {
            server_->get_user_list_async([this](std::vector<std::string> users) {
                std::string user_list = "* 접속자 목록:\r\n";
                for (const auto& user : users) {
                    user_list += "  - " + user + "\r\n";
                }
                deliver(user_list);
            });
        }
        else if (command == "/join") {
            std::string room_name;
            iss >> room_name;
            if (!room_name.empty()) {
                server_->join_room_async(room_name, shared_from_this(),
                    [self = shared_from_this(), room_name](bool success) {
                        if (success) {
                            self->deliver("* '" + room_name + "' 방에 입장했습니다.\r\n");
                        } else {
                            self->deliver("Error: 방 입장에 실패했습니다.\r\n");
                        }
                    });
            } else {
                deliver("Error: 사용법: /join <방이름>\r\n");
            }
        }
        else if (command == "/leave") {
            std::string room_name;
            iss >> room_name;
            if (!room_name.empty()) {
                if (server_->leave_room(room_name, shared_from_this())) {
                    deliver("* '" + room_name + "' 방에서 퇴장했습니다.\r\n");
                } else {
                    deliver("Error: 방 퇴장에 실패했습니다.\r\n");
                }
            } else {
                deliver("Error: 사용법: /leave <방이름>\r\n");
            }
        }
        else {
            deliver("Error: 알 수 없는 명령어입니다.\r\n");
        }
    }
    else {
        // 방에 메시지 보내기 또는 전역 브로드캐스트
        if (!current_room_.empty()) {
            server_->broadcast_to_room(current_room_, message, shared_from_this());
        } else {
            std::string broadcast_msg = "[" + nickname_ + "]: " + message;
            server_->broadcast(broadcast_msg, shared_from_this());
        }
    }
}

/**
 * @details `net::post`를 사용하여 세션의 `strand_`에서 안전하게 작업을 수행합니다.
 *          메시지를 `write_msgs_` 큐에 추가하고, 현재 쓰기 작업이 진행 중이 아니면
 *          `do_write`를 호출하여 메시지 전송을 시작합니다.
 *          큐가 가득 차면 경고를 남기고 메시지를 버립니다.
 */
void WebSocketSession::deliver(const std::string& msg)
{
    auto self = shared_from_this();
    net::post(strand_,
        [this, self, msg]() {
            // 큐 크기 제한 확인
            if (write_msgs_.size() >= max_queue_size_) {
                spdlog::warn("[WebSocketSession {}] Message queue full, dropping message", remote_id_);
                return;
            }
            
            bool write_in_progress = !write_msgs_.empty();
            write_msgs_.push(std::make_shared<const std::string>(msg));
            if (!write_in_progress && !is_writing_) {
                do_write();
            }
        });
}

/**
 * @details `write_msgs_` 큐가 비어있거나 이미 다른 쓰기 작업이 진행 중이면 아무것도 하지 않습니다.
 *          `is_writing_` 플래그를 설정하고 `ws_.async_write`를 호출하여 큐의 첫 번째 메시지를 전송합니다.
 */
void WebSocketSession::do_write()
{
    if (write_msgs_.empty() || is_writing_) {
        return;
    }
    
    is_writing_ = true;
    ws_.async_write(
        net::buffer(*write_msgs_.front()),
        beast::bind_front_handler(
            &WebSocketSession::on_write,
            std::enable_shared_from_this<WebSocketSession>::shared_from_this()));
}

/**
 * @details 쓰기 작업 완료 후 호출됩니다.
 *          - 에러 발생 시: 로그를 남기고 세션을 종료합니다.
 *          - 성공 시: 전송된 메시지를 큐에서 제거하고, 큐에 남은 메시지가 있으면 `do_write`를 다시 호출하여
 *            연속적으로 메시지를 전송합니다.
 */
void WebSocketSession::on_write(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);
    is_writing_ = false;
    
    if (ec) {
        spdlog::error("[WebSocketSession {}] Write failed: {}", remote_id_, ec.message());
        if (server_) {
            server_->leave(shared_from_this());
        }
        return;
    }
    
    write_msgs_.pop();
    
    if (!write_msgs_.empty()) {
        do_write();
    }
}

/**
 * @details `ws_.close`를 호출하여 WebSocket 연결 종료 핸드셰이크를 시작합니다.
 */
void WebSocketSession::stop_session()
{
    beast::error_code ec;
    ws_.close(websocket::close_code::normal, ec);
    if (ec) {
        spdlog::error("[WebSocketSession {}] Error closing WebSocket: {}", remote_id_, ec.message());
    }
}

void WebSocketSession::handle_auth(const std::string& username, const std::string& password)
{
    // TODO: 인증 로직 구현
    // 현재는 기본적으로 모든 연결을 허용
    authenticated_ = true;
}

 