#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp> ///< strand 사용 권장
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "handlers/PlacesApiHandler.hpp"

namespace beast = boost::beast;         ///< from <boost/beast.hpp>
namespace http = beast::http;           ///< from <boost/beast/http.hpp>
namespace net = boost::asio;            ///< from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       ///< from <boost/asio/ip/tcp.hpp>

// Forward declaration
class HttpSession; ///< 실제 구현은 HttpServer.cpp 에 있음

/**
 * @file HttpServer.hpp
 * @brief Boost.Beast를 이용한 비동기 HTTP 서버 관련 클래스 정의 헤더 파일.
 *
 * 이 파일은 HTTP 연결을 수락하는 `HttpListener` 클래스와
 * 전체 HTTP 서버의 생명주기 및 스레드 관리를 담당하는 `HttpServer` 클래스를 정의한다.
 * 실제 요청 처리는 `HttpSession` 클래스(`HttpServer.cpp`에 정의됨)가 담당한다.
 * Places API 요청은 `PlacesApiHandler`를 통해 처리된다.
 * @see HttpListener, HttpServer, HttpSession, PlacesApiHandler
 */

 /**
  * @class HttpListener
  * @brief 들어오는 HTTP 연결을 수락하고 각 연결에 대한 `HttpSession`을 생성하는 클래스.
  * @details `std::enable_shared_from_this`를 상속하여 비동기 작업 중에도 객체의 생존을 보장한다.
  * Acceptor는 strand를 통해 스레드 안전하게 실행되는 것을 권장한다.
  */
class HttpListener : public std::enable_shared_from_this<HttpListener>
{
    net::io_context& ioc_; ///< @brief 비동기 작업을 위한 Boost.Asio io_context 참조.
    tcp::acceptor acceptor_; ///< @brief 클라이언트 연결 요청을 수락하는 TCP acceptor. Strand 위에서 동작 권장.
    std::shared_ptr<PlacesApiHandler> places_handler_; ///< PlacesApiHandler 인스턴스 멤버 변수

public:
    /**
     * @brief HttpListener 생성자.
     *
     * Acceptor를 생성, 열고, 옵션 설정(reuse_address), 지정된 엔드포인트에 바인딩 및 리스닝을 시작한다.
     * @param ioc Boost.Asio io_context 참조.
     * @param endpoint 리슨할 로컬 TCP 엔드포인트 (IP 주소 및 포트).
     */
    HttpListener(
        net::io_context& ioc,
        tcp::endpoint endpoint);

    /**
     * @brief 리스너를 시작하여 비동기적으로 연결 수락을 시작한다.
     *
     * 내부적으로 `do_accept()`를 호출한다. 이 함수 호출 후 `io_context.run()`이 실행되어야 한다.
     */
    void run();

private:
    /**
     * @brief 비동기적으로 클라이언트 연결을 대기한다.
     *
     * `acceptor_`의 `async_accept`를 호출하여 연결을 기다린다.
     * 새 연결은 strand 위에서 처리하는 것이 권장된다 (`net::make_strand(ioc_)`).
     */
    void do_accept();

    /**
     * @brief 비동기 accept 작업 완료 시 호출되는 콜백 함수.
     *
     * @param ec 작업 결과 에러 코드.
     * @param socket 새로 생성된 클라이언트 소켓.
     * 성공 시, `HttpSession` 객체를 생성하고 `run()`을 호출하여 요청 처리를 시작한다.
     * 그 후, `do_accept()`를 다시 호출하여 다음 연결을 기다린다.
     */
    void on_accept(beast::error_code ec, tcp::socket socket);
};


/**
 * @class HttpServer
 * @brief Boost.Beast를 사용하여 HTTP 서버를 설정하고 실행하는 메인 래퍼 클래스.
 * @details 자체 `io_context`와 스레드 풀을 관리하여 HTTP 요청을 처리한다.
 * `HttpListener`를 생성하여 실제 연결 수락을 위임한다.
 */
class HttpServer {
public:
    /**
     * @brief HttpServer 생성자.
     * @param address 바인딩할 IP 주소 (예: "0.0.0.0").
     * @param port 리슨할 HTTP 포트 번호.
     * @param threads 사용할 IO 스레드 수. 0 이하일 경우 시스템 하드웨어 스레드 수를 기반으로 결정.
     */
    HttpServer(const std::string& address, unsigned short port, int threads);

    /**
     * @brief 서버를 시작한다.
     *
     * 지정된 주소와 포트로 리스닝하는 `HttpListener`를 생성하고 실행한다.
     * 지정된 수의 IO 스레드를 생성하여 내부 `io_context`의 `run()` 메서드를 실행시킨다.
     * 이 메서드는 즉시 반환될 수 있으며, 실제 서버 동작은 백그라운드 스레드에서 이루어진다.
     */
    void run();

    /**
     * @brief 서버를 정상적으로 중지한다.
     *
     * 내부 `io_context`를 중지시키고, 모든 IO 스레드가 종료될 때까지 기다린다.
     * Listener 및 관련 리소스를 정리한다.
     */
    void stop();

private:
    std::string address_;       ///< @brief 서버가 바인딩될 IP 주소.
    unsigned short port_;       ///< @brief 서버가 리슨할 포트 번호.
    int threads_;               ///< @brief IO 작업을 처리할 스레드 수.

    net::io_context ioc_; ///< @brief Beast HTTP 서버용 자체 io_context. 스레드 수를 생성 시 지정.
    std::vector<std::thread> io_threads_; ///< @brief io_context를 실행하는 IO 스레드들.
    std::shared_ptr<HttpListener> listener_{ nullptr }; ///< @brief HTTP 연결을 수락하는 리스너 객체.
};