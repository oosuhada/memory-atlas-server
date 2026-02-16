// include/ChatListener.hpp
#pragma once

#include <memory>
#include <string>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/error.hpp> // For beast::error_code

// Forward declarations
class ChatServer;
class ChatSession; // Used in on_accept
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;

/**
 * @class ChatListener
 * @brief 들어오는 채팅 클라이언트 연결을 수락하고 ChatSession을 생성.
 * 
 * HttpServer의 HttpListener와 유사한 역할.
 */
class ChatListener : public std::enable_shared_from_this<ChatListener>
{
    /// @brief 비동기 I/O 작업을 위한 io_context.
    net::io_context& ioc_;
    /// @brief 들어오는 연결을 수락하는 acceptor.
    tcp::acceptor acceptor_;
    /// @brief ChatServer의 공유 포인터. 세션 생성 시 필요.
    std::shared_ptr<ChatServer> server_;

public:
    /**
     * @brief ChatListener 생성자.
     * @param ioc 비동기 작업에 사용할 io_context 참조.
     * @param endpoint 리슨할 TCP 엔드포인트.
     * @param server ChatServer 참조.
     */
    ChatListener(
        net::io_context& ioc,
        tcp::endpoint endpoint,
        std::shared_ptr<ChatServer> server);

    /** 
     * @brief 리스너를 시작하여 비동기 연결 수락 시작.
     * 내부적으로 do_accept를 호출한다.
     */
    void run();

private:
    /** 
     * @brief 비동기 accept 루프.
     * 새 클라이언트 연결을 수락하기 위해 acceptor의 async_accept를 호출한다.
     */
    void do_accept();

    /** 
     * @brief accept 완료 콜백.
     * @param ec 오류 코드.
     * @param socket 새로 만들어진 클라이언트 소켓.
     * 성공 시 ChatSession을 생성하고 시작한다.
     */
    void on_accept(beast::error_code ec, tcp::socket socket);
};