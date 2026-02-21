#include <gtest/gtest.h>
#include "../include/HttpServer.hpp" // 테스트 대상 HttpServer 클래스 헤더

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp> // boost::asio::connect
#include <boost/asio/ip/tcp.hpp> // tcp::socket, tcp::resolver
#include <boost/asio/io_context.hpp> // io_context
#include <boost/system/error_code.hpp> // boost::system::error_code
#include <boost/asio/error.hpp> // boost::asio::error 네임스페이스

#include <string>
#include <thread>
#include <chrono> // std::chrono 네임스페이스
#include <memory> // std::unique_ptr
#include <cstdio> // fprintf (로깅용)
#include <sstream> // std::stringstream (스레드 ID 로깅용)
#include <stdexcept> // std::exception 등

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/**
 * @file test_http_server.cpp
 * @brief `HttpServer` 클래스의 기능을 검증하는 Google Test 테스트 파일.
 *
 * `HttpServerTest` Fixture 클래스를 사용하여 각 테스트 전에 서버를 설정하고,
 * 테스트 종료 후 정리한다. 현재 `/health` 엔드포인트와 존재하지 않는 경로(404)를 테스트한다.
 * @see HttpServer, HttpServerTest
 */

 // --- HTTP 서버 테스트를 위한 Fixture 클래스 ---
 /**
  * @class HttpServerTest
  * @brief `HttpServer` 테스트를 위한 Google Test Fixture 클래스.
  * @details 각 테스트 케이스 실행 전에 `SetUp()` 메서드를 호출하여 `HttpServer` 인스턴스를
  * 생성하고 내부 스레드에서 실행시킨다. 테스트 종료 후 `TearDown()` 메서드를 호출하여
  * `HttpServer::stop()`을 통해 서버를 정상 종료하고 관련 리소스를 정리한다.
  * 테스트용 HTTP GET 요청 함수(`http_get`)를 제공한다.
  */
class HttpServerTest : public ::testing::Test {
protected:
    // 테스트 설정 값
    const unsigned short test_port = 8080; ///< @brief 테스트에 사용할 HTTP 포트 번호.
    const std::string test_ip = "127.0.0.1"; ///< @brief 테스트 서버가 리슨할 IP 주소 (localhost).
    const int test_threads = 1; // 테스트에는 스레드 1개로도 충분할 수 있음.
    // const std::string test_doc_root = "."; // 사용되지 않으므로 주석 처리

    // 서버 객체 포인터
    std::unique_ptr<HttpServer> server;
    // std::unique_ptr<net::io_context> ioc_; // HttpServer가 자체 ioc 관리 가정
    // std::unique_ptr<tcp::socket> client_socket_; // http_get 내부에서 생성
    // tcp::resolver::results_type endpoints_; // http_get 내부에서 사용
    // std::thread server_thread_; // HttpServer가 자체 스레드 관리 가정

    /**
     * @brief 각 테스트 케이스 시작 전에 호출되는 설정 메서드.
     *
     * `HttpServer` 객체를 생성하고 `run()` 메서드를 호출하여 서버를 시작한다.
     * 서버가 내부적으로 리스닝을 시작할 때까지 잠시 대기한다.
     * @note 서버 준비 완료를 더 확실하게 확인하려면 서버 내부에서 신호를 보내거나,
     * 반복적으로 접속 시도를 해보는 방식이 더 견고하다. 여기서는 시간 기반 대기 사용.
     */
    void SetUp() override {
        fprintf(stdout, "[HttpServerTest::SetUp] Starting setup for port %d...\n", test_port);
        try {
            // 서버 생성 시 올바른 생성자 사용 (주소, 포트, 스레드 수)
            server = std::make_unique<HttpServer>(test_ip, test_port, test_threads);
            
            // 별도 스레드에서 서버 실행
            std::thread server_thread([this]() {
                try {
                    server->run(); // run() 내부에서 ioc_.run() 호출 가정
                } catch (const std::exception& e) {
                    fprintf(stderr, "Server thread exception: %s\n", e.what());
                }
            });
            server_thread.detach(); // 스레드 분리 (서버는 TearDown에서 중지됨)
            
            // 서버 시작 대기 (간단한 sleep, 실제로는 health check 등 사용 권장)
            // Add a delay to allow the server thread to initialize and start listening
            fprintf(stdout, "[HttpServerTest::SetUp] Waiting for server to initialize...\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 500ms로 다시 줄임
            
            // 테스트용 클라이언트 준비
            // client_socket_ = std::make_unique<tcp::socket>(*ioc_);
            // tcp::resolver resolver(*ioc_);
            // endpoints_ = resolver.resolve(test_ip, std::to_string(test_port));

            fprintf(stdout, "[HttpServerTest::SetUp] Setup complete, assuming server is running on port %d.\n", test_port);

        }
        catch (const std::exception& e) {
            fprintf(stderr, "[HttpServerTest::SetUp] Exception during setup: %s\n", e.what());
            // 서버 객체가 생성되었을 수 있으므로 정리 시도
            if (server) server->stop(); // 예외 발생 시에도 stop 시도
            server.reset();
            FAIL() << "HttpServer SetUp failed: " << e.what();
        }
    }

    /**
     * @brief 각 테스트 케이스 종료 후 호출되는 정리 메서드.
     *
     * `HttpServer::stop()` 메서드를 호출하여 서버를 정상적으로 종료시킨다.
     * `stop()`은 내부적으로 io_context 중지 및 모든 IO 스레드 join을 수행한다.
     */
    void TearDown() override {
        fprintf(stdout, "[HttpServerTest::TearDown] Starting teardown for port %d...\n", test_port);
        if (server) {
            fprintf(stdout, "[HttpServerTest::TearDown] Calling server->stop()...\n");
            server->stop(); // 서버 중지 (내부 스레드 join 포함)
            fprintf(stdout, "[HttpServerTest::TearDown] server->stop() returned.\n");
            server.reset(); // unique_ptr 리셋 (객체 소멸)
            fprintf(stdout, "[HttpServerTest::TearDown] Server object reset.\n");
        }
        else {
            fprintf(stdout, "[HttpServerTest::TearDown] Server pointer was null, nothing to stop.\n");
        }
        fprintf(stdout, "[HttpServerTest::TearDown] Teardown complete.\n");
    }

    /**
     * @brief 테스트용 간단한 동기 HTTP GET 요청 함수.
     * @param target 요청할 경로 (예: "/health", "/nonexistent").
     * @return 서버로부터 받은 HTTP 응답 객체 (`http::response<http::string_body>`).
     * 오류 발생 시 상태 코드가 500 Internal Server Error로 설정되고 본문에 오류 메시지가 포함될 수 있다.
     *
     * localhost의 `test_port`로 TCP 연결을 시도하고, 지정된 경로로 HTTP GET 요청을 보낸 후,
     * 서버로부터 응답을 받아 반환한다. Boost.Beast의 동기 함수 사용.
     */
    http::response<http::string_body> http_get(const std::string& target) {
        http::response<http::string_body> res; // 응답 객체
        boost::system::error_code ec;          // 오류 코드 저장

        try {
            net::io_context ioc; // 클라이언트용 io_context
            tcp::resolver resolver(ioc); // 주소 해석기
            beast::tcp_stream stream(ioc); // 클라이언트 TCP 스트림 (Beast 사용)

            // 서버 주소 해석
            fprintf(stdout, "[Test HTTP Client] Resolving %s:%d...\n", test_ip.c_str(), test_port);
            auto const results = resolver.resolve(test_ip, std::to_string(test_port), ec);
            if (ec) throw beast::system_error{ ec, "Resolve failed" };


            // 서버에 동기적으로 연결
            fprintf(stdout, "[Test HTTP Client] Connecting to %s:%d...\n", test_ip.c_str(), test_port);
            stream.connect(results, ec);
            if (ec) throw beast::system_error{ ec, "Connect failed" };
            fprintf(stdout, "[Test HTTP Client] Connected.\n");

            // HTTP GET 요청 메시지 생성
            fprintf(stdout, "[Test HTTP Client] Sending GET request for target: %s\n", target.c_str());
            http::request<http::empty_body> req{ http::verb::get, target, 11 }; // HTTP/1.1 사용
            req.set(http::field::host, test_ip + ":" + std::to_string(test_port)); // Host 헤더 설정
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING); // User-Agent 헤더 설정

            // 동기적으로 요청 전송
            http::write(stream, req, ec);
            if (ec) throw beast::system_error{ ec, "Write failed" };
            fprintf(stdout, "[Test HTTP Client] Request sent.\n");

            // 응답 수신을 위한 버퍼
            beast::flat_buffer buffer;

            // 동기적으로 응답 읽기 (응답 헤더 + 본문)
            fprintf(stdout, "[Test HTTP Client] Reading response...\n");
            http::read(stream, buffer, res, ec);
            // EOF는 정상적인 응답 수신 후 서버가 연결을 닫는 경우일 수 있음 (오류 아님)
            if (ec && ec != http::error::end_of_stream) {
                throw beast::system_error{ ec, "Read failed" };
            }
            fprintf(stdout, "[Test HTTP Client] Response received (Status: %d, %zu bytes body).\n", res.result_int(), res.body().size());


            // 소켓 정상 종료 (graceful close)
            fprintf(stdout, "[Test HTTP Client] Shutting down and closing stream...\n");
            // stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            // shutdown 오류는 무시 (이미 서버가 닫았을 수 있음)
            // if(ec && ec != beast::errc::not_connected) {
            //     // 오류 로깅 또는 예외 처리 (선택적)
            //      fprintf(stderr, "[Test HTTP Client] Shutdown warning: %s\n", ec.message().c_str());
            //     // throw beast::system_error{ec, "Shutdown failed"};
            // }
             // 스트림 닫기 (소멸 시 자동으로 닫히지만 명시적 호출 가능)
             // stream.close(ec);
            if (ec) {
                fprintf(stderr, "[Test HTTP Client] Close warning: %s\n", ec.message().c_str());
            }
            fprintf(stdout, "[Test HTTP Client] Stream closed.\n");


        }
        catch (const beast::system_error& e) {
            fprintf(stderr, "[Test HTTP Client] System Error: %s (code: %d)\n", e.what(), e.code().value());
            // 오류 발생 시 응답 상태 코드를 비정상적인 값으로 설정하여 테스트 실패 유도
            res.result(http::status::internal_server_error); // 500 Internal Server Error
            res.body() = "[Client Error] " + std::string(e.what());
            res.prepare_payload(); // Content-Length 설정
            ADD_FAILURE() << "HTTP GET client operation failed: " << e.what();
        }
        catch (const std::exception& e) {
            fprintf(stderr, "[Test HTTP Client] Standard Exception: %s\n", e.what());
            res.result(http::status::internal_server_error);
            res.body() = "[Client Error] " + std::string(e.what());
            res.prepare_payload();
            ADD_FAILURE() << "HTTP GET client operation failed: " << e.what();
        }
        return res; // 성공 시 서버 응답, 실패 시 오류 정보 포함된 응답 반환
    }
}; // end class HttpServerTest

// --- 테스트 케이스 정의 ---

/**
 * @brief `/health` 경로 요청 시 200 OK와 "OK" 본문을 반환하는지 테스트.
 *
 * `http_get("/health")`를 호출하여 응답을 받고,
 * 상태 코드, Content-Type 헤더, 본문 내용이 예상과 일치하는지 검증한다.
 */
TEST_F(HttpServerTest, HealthCheckReturnsOK) {
    // Arrange (Fixture SetUp에서 서버 실행됨)
    SCOPED_TRACE("Test: HealthCheckReturnsOK"); // 실패 시 추가 정보

    // Act: /health 경로로 GET 요청 보내기
    auto response = http_get("/health");

    // Assert: 응답 검증
    // 1. 상태 코드가 200 OK 인지 확인
    EXPECT_EQ(response.result(), http::status::ok);
    // 추가 검증: 정수 값으로도 확인
    EXPECT_EQ(response.result_int(), 200);

    // 2. Content-Type 헤더가 "text/plain" 인지 확인 (대소문자 구분 안 함 비교 권장)
    ASSERT_TRUE(response.count(http::field::content_type)); // 헤더 존재 여부 확인
    // EXPECT_EQ(response[http::field::content_type], "text/plain"); // 정확한 문자열 비교
     // 대소문자 무시 비교 예시 (직접 구현 또는 Boost String Algo 사용)
    std::string content_type = std::string(response[http::field::content_type]);
    std::transform(content_type.begin(), content_type.end(), content_type.begin(), ::tolower);
    EXPECT_EQ(content_type, "text/plain");


    // 3. 응답 본문이 정확히 "OK" 인지 확인
    EXPECT_EQ(response.body(), "OK");

    // 4. 서버 헤더 존재 여부 확인 (선택적)
    EXPECT_TRUE(response.count(http::field::server));
}

/**
 * @brief 존재하지 않는 경로 요청 시 404 Not Found를 반환하는지 테스트.
 *
 * `http_get("/nonexistentpath")`를 호출하여 응답을 받고,
 * 상태 코드가 404 Not Found 이고, 본문에 "not found" (대소문자 무관) 문자열이 포함되어 있는지 검증한다.
 */
TEST_F(HttpServerTest, NotFoundReturns404) {
    // Arrange
    std::string non_existent_path = "/some/random/path/that/does/not/exist";
    SCOPED_TRACE("Test: NotFoundReturns404, Path: " + non_existent_path);

    // Act: 존재하지 않는 경로로 GET 요청 보내기
    auto response = http_get(non_existent_path);

    // Assert: 응답 검증
    // 1. 상태 코드가 404 Not Found 인지 확인
    EXPECT_EQ(response.result(), http::status::not_found);
    EXPECT_EQ(response.result_int(), 404);

    // 2. 응답 본문에 "not found" (대소문자 무시) 문자열이 포함되어 있는지 확인
    std::string body_lower = response.body();
    std::transform(body_lower.begin(), body_lower.end(), body_lower.begin(), ::tolower);
    EXPECT_NE(body_lower.find("not found"), std::string::npos) << "Response body does not contain 'not found'. Body: " << response.body();

    // 3. Content-Type 헤더 확인 (선택적, 보통 text/plain 또는 text/html)
    ASSERT_TRUE(response.count(http::field::content_type));
    std::string content_type_404 = std::string(response[http::field::content_type]);
    std::transform(content_type_404.begin(), content_type_404.end(), content_type_404.begin(), ::tolower);
    EXPECT_NE(content_type_404.find("text/plain"), std::string::npos) << "Expected Content-Type 'text/plain' for 404 error."; // 예시: text/plain 기대

    // 4. 서버 헤더 존재 여부 확인 (선택적)
    EXPECT_TRUE(response.count(http::field::server));
}

// TODO: 추가적인 HTTP 서버 테스트 케이스 제안
// - POST, PUT 등 다른 HTTP 메소드 테스트 (서버 구현 시)
// - 잘못된 형식의 HTTP 요청 테스트 (Bad Request 400)
// - Keep-Alive 동작 테스트 (한 연결에서 여러 요청 처리)
// - 동시 요청 처리 테스트 (여러 스레드에서 동시에 http_get 호출)
// - 정적 파일 제공 기능 테스트 (구현 시)