/**
 * @file ChatSession.hpp
 * @brief TCP 클라이언트와의 채팅 세션을 관리하는 클래스를 정의합니다.
 * @details 각 클라이언트는 하나의 `ChatSession` 인스턴스를 가지며,
 * 이 클래스는 메시지 수신, 처리, 전송 등 클라이언트와의 모든 상호작용을 담당합니다.
 * 비동기 I/O 작업을 위해 Boost.Asio를 사용하며, 스레드 안전성을 위해 strand를 활용합니다.
 */
#pragma once

#include <memory>
#include <string>
#include <deque>
#include <atomic>
#include <vector>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/core/error.hpp> // For beast::error_code
#include "SessionInterface.hpp"

// Forward declarations
class ChatServer;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;

/**
 * @class ChatSession
 * @brief 개별 TCP 클라이언트와의 통신을 담당하는 클래스.
 * @details SessionInterface를 구현하며, 클라이언트의 연결부터 종료까지 전체 생명주기를 관리합니다.
 *          메시지 수신, 명령어 처리, 메시지 전송 큐 관리 등의 기능을 수행합니다.
 * @see SessionInterface
 * @see ChatServer
 */
class ChatSession : public SessionInterface, public std::enable_shared_from_this<ChatSession> {
    friend class ChatServer; // Allow ChatServer access if needed (e.g., socket)
private:
    tcp::socket socket_;
    std::shared_ptr<ChatServer> server_;
    net::strand<net::any_io_executor> strand_;
    boost::asio::streambuf read_buffer_;
    std::deque<std::string> write_msgs_;
    bool writing_flag_ = false;
    std::string remote_id_;
    std::string nickname_;
    std::string current_room_;
    std::atomic<bool> stopped_{false};
    bool authenticated_ = false; // SessionInterface requires this

public:
    /**
     * @brief ChatSession 생성자.
     * @param socket 클라이언트와 연결된 TCP 소켓. `std::move`를 통해 소유권이 이전됩니다.
     * @param server 세션이 속한 `ChatServer`의 `shared_ptr`.
     */
    explicit ChatSession(tcp::socket socket, std::shared_ptr<ChatServer> server);
    ~ChatSession();

    /**
     * @brief 세션 처리를 시작합니다.
     * @details ChatServer에 세션을 등록하고, 환영 메시지를 보낸 후 비동기 읽기 작업을 시작합니다.
     */
    void start();
    
    // SessionInterface implementation
    /**
     * @brief 세션을 중지하고 연결을 종료합니다.
     * @details 비동기 작업을 취소하고 소켓을 안전하게 닫습니다. `ChatServer`에게 세션 종료를 알립니다.
     * @override
     */
    void stop_session() override;
    /**
     * @brief 클라이언트에게 메시지를 전송합니다.
     * @details 메시지를 전송 큐에 추가하고, 쓰기 작업이 진행 중이 아니면 비동기 쓰기 작업을 시작합니다.
     *          이 메서드는 스레드에 안전합니다.
     * @param msg 전송할 메시지 문자열.
     * @override
     */
    void deliver(const std::string& msg) override;
    /**
     * @brief 현재 세션의 닉네임을 반환합니다.
     * @return const std::string& 닉네임.
     * @override
     */
    const std::string& nickname() const override;
    /**
     * @brief 현재 세션의 원격 ID(IP:Port)를 반환합니다.
     * @return const std::string& 원격 ID.
     * @override
     */
    const std::string& remote_id() const override;
    /**
     * @brief 세션의 스트랜드를 반환합니다.
     * @details 외부에서 세션의 이벤트 핸들러를 안전하게 post/dispatch 할 수 있도록 합니다.
     * @return net::strand<net::any_io_executor>& 세션의 스트랜드.
     * @override
     */
    net::strand<net::any_io_executor>& get_strand() override { return strand_; }
    /**
     * @brief 세션의 인증 상태를 확인합니다. (현재 구현에서는 항상 false)
     * @return bool 인증 여부.
     * @override
     */
    bool is_authenticated() const override { return authenticated_; }
    /**
     * @brief 세션의 닉네임을 설정합니다.
     * @param nick 새로운 닉네임.
     * @override
     */
    void set_nickname(const std::string& nick) override { nickname_ = nick; }
    /**
     * @brief 세션의 인증 상태를 설정합니다.
     * @param auth 새로운 인증 상태.
     * @override
     */
    void set_authenticated(bool auth) override { authenticated_ = auth; }
    /**
     * @brief 현재 참여 중인 채팅방 이름을 반환합니다.
     * @return const std::string& 채팅방 이름.
     * @override
     */
    const std::string& current_room() const override;
    /**
     * @brief 참여할 채팅방 이름을 설정합니다.
     * @param room_name 새로운 채팅방 이름.
     * @override
     */
    void set_current_room(const std::string& room_name) override;
    /**
     * @brief `enable_shared_from_this`를 통해 `shared_ptr`을 얻습니다. (SessionInterface 호환용)
     * @return std::shared_ptr<SessionInterface>
     * @override
     */
    std::shared_ptr<SessionInterface> shared_from_this() override {
        return shared_from_this();
    }
    
    /**
     * @brief 이 ChatSession 객체에 대한 `shared_ptr`를 반환합니다.
     * @details `enable_shared_from_this`를 사용하여 안전하게 `shared_ptr`를 생성합니다.
     * @return std::shared_ptr<ChatSession>
     */
    std::shared_ptr<ChatSession> shared_from_this_chat() {
        return std::static_pointer_cast<ChatSession>(shared_from_this());
    }

private:
    /**
     * @brief 클라이언트로부터 받은 메시지를 처리합니다.
     * @details 메시지가 명령어('/'로 시작)인지 일반 채팅 메시지인지 구분하여 처리합니다.
     * @param command_line 클라이언트로부터 받은 한 줄의 메시지.
     */
    void process_command(const std::string& command_line);
    /**
     * @brief 클라이언트로부터 비동기적으로 데이터를 읽기 시작합니다.
     * @details 개행 문자('\n')를 만날 때까지 데이터를 읽습니다.
     */
    void do_read();
    /**
     * @brief 비동기 읽기 작업의 완료 콜백 핸들러.
     * @param ec 작업 결과 에러 코드.
     * @param length 읽은 데이터의 길이.
     */
    void on_read(beast::error_code ec, std::size_t length);
    /**
     * @brief 전송 큐에 있는 메시지를 비동기적으로 쓰기 시작합니다.
     * @details 큐의 가장 앞에 있는 메시지를 소켓을 통해 클라이언트에게 전송합니다.
     *          이 메서드는 반드시 스트랜드 내에서 호출되어야 합니다.
     */
    void do_write_strand();
    /**
     * @brief 비동기 쓰기 작업의 완료 콜백 핸들러.
     * @param ec 작업 결과 에러 코드.
     * @param length 전송한 데이터의 길이.
     */
    void on_write(std::error_code ec, std::size_t length);
};