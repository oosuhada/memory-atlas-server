#ifndef WEBSOCKET_LISTENER_HPP
#define WEBSOCKET_LISTENER_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <memory>
#include <optional>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class ChatServer;

/// WebSocket 연결을 수신하고 WebSocketSession을 생성하는 클래스
class WebSocketListener : public std::enable_shared_from_this<WebSocketListener>
{
private:
    /// @brief 비동기 I/O 작업을 위한 io_context.
    net::io_context& ioc_;
    /// @brief 들어오는 TCP 연결을 수락하는 acceptor.
    tcp::acceptor acceptor_;
    /// @brief ChatServer의 공유 포인터. 세션 생성 시 필요.
    std::shared_ptr<ChatServer> server_;

public:
    // HTTP/WS용 생성자
    WebSocketListener(net::io_context& ioc, 
                      tcp::endpoint endpoint, 
                      std::shared_ptr<ChatServer> server);

    
    // Start accepting connections
    void run();
    
private:
    /**
     * @brief 주어진 엔드포인트로 acceptor를 초기화한다.
     * @param endpoint 리슨할 TCP 엔드포인트.
     */
    void init_acceptor(tcp::endpoint endpoint);
    /**
     * @brief 비동기 accept 루프를 시작한다.
     */
    void do_accept();
    /**
     * @brief accept 완료 시 호출되는 콜백 함수.
     * @param ec 오류 코드.
     * @param socket 새로 생성된 클라이언트 소켓.
     */
    void on_accept(beast::error_code ec, tcp::socket socket);
};

#endif // WEBSOCKET_LISTENER_HPP 