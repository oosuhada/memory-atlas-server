/**
 * @file WebSocketSession.hpp
 * @brief WebSocket 클라이언트와의 채팅 세션을 관리하는 클래스를 정의합니다.
 * @details 이 클래스는 Boost.Beast 라이브러리를 사용하여 WebSocket 프로토콜을 처리합니다.
 *          `SessionInterface`를 구현하여 `ChatServer`가 TCP 세션과 동일한 방식으로
 *          WebSocket 세션을 관리할 수 있도록 합니다.
 */
#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#include "SessionInterface.hpp"
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include <queue>
#include <string>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class ChatServer;

/**
 * @class WebSocketSession
 * @brief 개별 WebSocket 클라이언트와의 통신을 담당하는 클래스.
 * @details `SessionInterface`를 구현하며, WebSocket 핸드셰이크, 메시지의 비동기 읽기/쓰기,
 *          명령어 처리 등 WebSocket 기반 통신의 전체 생명주기를 관리합니다.
 * @see SessionInterface
 * @see ChatServer
 */
class WebSocketSession
    : public SessionInterface,
      public std::enable_shared_from_this<WebSocketSession> {
private:
  websocket::stream<beast::tcp_stream> ws_; ///< WebSocket 스트림 객체
  beast::flat_buffer buffer_; ///< 메시지 읽기를 위한 버퍼
  std::shared_ptr<ChatServer> server_; ///< 세션이 속한 ChatServer에 대한 포인터
  net::strand<net::any_io_executor> strand_; ///< 세션 내 비동기 핸들러의 직렬 실행을 보장하는 스트랜드
  std::queue<std::shared_ptr<const std::string>> write_msgs_; ///< 전송 대기 중인 메시지를 저장하는 큐
  bool is_writing_ = false; ///< 현재 쓰기 작업이 진행 중인지 나타내는 플래그
  
  std::string nickname_;     ///< 클라이언트가 설정한 닉네임
  std::string remote_id_;    ///< 클라이언트의 IP 주소와 포트로 구성된 고유 식별자
  std::string current_room_; ///< 현재 참여 중인 채팅방 이름
  bool authenticated_ = false; ///< 인증 상태 (현재는 사용되지 않음)

  // 보안 및 리소스 관리 설정
  static constexpr std::size_t max_message_size_ = 1024 * 1024; // 1MB 메시지 크기 제한
  static constexpr std::size_t max_queue_size_ = 100; // 최대 전송 대기 큐 크기

public:
  /**
   * @brief WebSocketSession 생성자.
   * @param socket 클라이언트와 연결된 TCP 소켓. `std::move`를 통해 `ws_` 멤버로 이전됩니다.
   * @param server 세션이 속한 `ChatServer`의 `shared_ptr`.
   */
  explicit WebSocketSession(tcp::socket &&socket,
                            std::shared_ptr<ChatServer> server);

  /**
   * @brief WebSocketSession 소멸자.
   */
  ~WebSocketSession();

  /**
   * @brief 세션의 비동기 작업을 시작합니다.
   * @details WebSocket 핸드셰이크를 시작하여 프로토콜 업그레이드를 시도합니다.
   */
  void run();

  // --- SessionInterface 구현부 ---

  /**
   * @brief 클라이언트에게 메시지를 전송 큐에 추가합니다.
   * @param msg 전송할 메시지 문자열.
   * @override
   */
  void deliver(const std::string &msg) override;

  /**
   * @brief 세션을 중지하고 WebSocket 연결을 정상적으로 닫습니다.
   * @override
   */
  void stop_session() override;

  // (다른 SessionInterface 멤버들의 주석은 ChatSession.hpp 와 유사하므로 생략)
  const std::string &nickname() const override { return nickname_; }
  const std::string &remote_id() const override { return remote_id_; }
  net::strand<net::any_io_executor> &get_strand() override { return strand_; }
  bool is_authenticated() const override { return authenticated_; }
  void set_nickname(const std::string &nick) override { nickname_ = nick; }
  void set_authenticated(bool auth) override { authenticated_ = auth; }
  const std::string &current_room() const override { return current_room_; }
  void set_current_room(const std::string &room_name) override {
    current_room_ = room_name;
  }
  std::shared_ptr<SessionInterface> shared_from_this() override {
    return std::static_pointer_cast<SessionInterface>(
        std::enable_shared_from_this<WebSocketSession>::shared_from_this());
  }

private:
  /**
   * @brief WebSocket 핸드셰이크 수락 완료 시 호출되는 콜백.
   * @param ec 작업 결과 에러 코드.
   */
  void on_accept(beast::error_code ec);

  /**
   * @brief 클라이언트로부터 비동기적으로 메시지를 읽기 시작합니다.
   */
  void do_read();

  /**
   * @brief 비동기 읽기 작업 완료 시 호출되는 콜백.
   * @param ec 작업 결과 에러 코드.
   * @param bytes_transferred 읽은 데이터의 길이.
   */
  void on_read(beast::error_code ec, std::size_t bytes_transferred);

  /**
   * @brief 전송 큐에 있는 다음 메시지를 비동기적으로 쓰기 시작합니다.
   */
  void do_write();

  /**
   * @brief 비동기 쓰기 작업 완료 시 호출되는 콜백.
   * @param ec 작업 결과 에러 코드.
   * @param bytes_transferred 전송한 데이터의 길이.
   */
  void on_write(beast::error_code ec, std::size_t bytes_transferred);

  /**
   * @brief 클라이언트로부터 수신한 메시지를 파싱하고 처리합니다.
   * @details 메시지가 명령어인지 일반 채팅 메시지인지 구분하여 적절한 동작을 수행합니다.
   * @param message 수신된 메시지 문자열.
   */
  void process_message(const std::string &message);

  /**
   * @brief 사용자 인증을 처리합니다. (현재는 구현되지 않음)
   * @param username 사용자 이름.
   * @param password 비밀번호.
   */
  void handle_auth(const std::string& username, const std::string& password);
}; 