#include "HttpServer.hpp"          // HTTP Server 헤더 (Beast)
#include "../include/ChatServer.hpp"  // ChatServer 추가
#include <boost/asio/io_context.hpp> // Asio io_context
#include <boost/asio/signal_set.hpp> // Asio signal_set
#include <boost/beast/core/error.hpp> // beast::error_code
#include <cstdio>                  // fprintf, snprintf 등 C-style I/O
#include <cstdlib>                 // getenv, exit, stoi (C++11)
#include <iostream>
#include <stdexcept>               // runtime_error
#include <string>
#include <thread>                  // std::thread
#include <vector>
#include <csignal>                 // signal, SIGINT, SIGTERM
#include <atomic>                  // std::atomic_bool
#include <memory>                  // std::unique_ptr, std::make_shared
#include <system_error>            // std::system_error (예외 처리)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // SetConsoleOutputCP, SetConsoleCP
#include <locale>    // std::locale::global
#endif

// --- 추가된 include ---
#include "../include/ChatServer.hpp"         // ChatServer 추가
#include "../include/WebSocketListener.hpp"  // WebSocket Listener 추가

// --- 네임스페이스 별칭 ---
// Boost.Asio와 Beast를 더 간결하게 사용하기 위함
namespace net = boost::asio;
namespace beast = boost::beast;

/**
 * @file main.cpp
 * @brief CherryRecorder 서버 애플리케이션의 메인 진입점 파일.
 *
 * 환경 변수에서 설정을 읽어 HTTP 서버와 Chat 서버를 생성하고 실행한다.
 * POSIX 시그널(SIGINT, SIGTERM)을 처리하여 서버의 정상 종료(graceful shutdown)를 지원한다.
 * Windows 환경에서는 콘솔 입출력 인코딩을 UTF-8로 설정하려고 시도한다.
 */

 // --- 환경 변수 처리 헬퍼 함수 ---

 /**
  * @brief 환경 변수에서 포트 번호를 읽어온다. 없으면 기본값을 사용한다.
  * @param var_name 읽어올 환경 변수 이름.
  * @param default_port 환경 변수가 없거나 유효하지 않을 경우 사용할 기본 포트 번호.
  * @return 읽어온 포트 번호 (unsigned short).
  * @throw std::runtime_error 환경 변수 값이 있으나 유효한 포트 범위(1-65535)가 아니거나 숫자로 변환 불가능한 경우.
  */
unsigned short get_required_port_env_var(const std::string& var_name, unsigned short default_port) {
    const char* value_str = std::getenv(var_name.c_str());
    if (value_str == nullptr) {
        fprintf(stdout, "Environment variable '%s' not set. Using default value: %hu\n", var_name.c_str(), default_port);
        return default_port;
    }
    try {
        int port_int = std::stoi(value_str); // C++11 stoi 사용
        if (port_int > 0 && port_int <= 65535) {
            fprintf(stdout, "Read environment variable '%s': %d\n", var_name.c_str(), port_int);
            return static_cast<unsigned short>(port_int);
        }
        else {
            // 오류 메시지 개선
            char msg[256];
            snprintf(msg, sizeof(msg), "Environment variable '%s' value '%s' is out of valid port range (1-65535).", var_name.c_str(), value_str);
            throw std::runtime_error(msg);
        }
    }
    catch (const std::invalid_argument& ia) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to convert environment variable '%s' value '%s' to integer: %s", var_name.c_str(), value_str, ia.what());
        throw std::runtime_error(msg);
    }
    catch (const std::out_of_range& oor) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Environment variable '%s' value '%s' is out of integer range: %s", var_name.c_str(), value_str, oor.what());
        throw std::runtime_error(msg);
    }
    // catch (const std::exception& e) 와 동일한 효과지만 더 명시적
}

/**
 * @brief 환경 변수에서 문자열 값을 읽어온다. 없으면 기본값을 사용한다.
 * @param var_name 읽어올 환경 변수 이름.
 * @param default_value 환경 변수가 없을 경우 사용할 기본 문자열 값.
 * @return 읽어온 문자열 값.
 */
std::string get_env_var(const std::string& var_name, const std::string& default_value) {
    const char* value = std::getenv(var_name.c_str());
    if (value == nullptr) {
        fprintf(stdout, "Environment variable '%s' not set. Using default value: '%s'\n", var_name.c_str(), default_value.c_str());
        return default_value;
    }
    fprintf(stdout, "Read environment variable '%s': '%s'\n", var_name.c_str(), value);
    return std::string(value);
}

/**
 * @brief 환경 변수에서 정수 값을 읽어온다. 없거나 유효하지 않으면 기본값을 사용한다.
 * @param var_name 읽어올 환경 변수 이름.
 * @param default_value 환경 변수가 없거나 정수로 변환 불가능할 경우 사용할 기본 정수 값.
 * @return 읽어온 정수 값.
 */
int get_int_env_var(const std::string& var_name, int default_value) {
    const char* value_str = std::getenv(var_name.c_str());
    if (value_str == nullptr) {
        fprintf(stdout, "Environment variable '%s' not set. Using default value: %d\n", var_name.c_str(), default_value);
        return default_value;
    }
    try {
        int value_int = std::stoi(value_str);
        fprintf(stdout, "Read environment variable '%s': %d\n", var_name.c_str(), value_int);
        return value_int;
    }
    catch (const std::exception& e) { // stoi 실패 (invalid_argument, out_of_range)
        fprintf(stderr, "Warning: Failed to convert environment variable '%s' value '%s' to integer (%s). Using default value: %d\n",
            var_name.c_str(), value_str, e.what(), default_value);
        return default_value;
    }
}

/**
 * @brief 애플리케이션 메인 함수.
 * @param argc 커맨드 라인 인자 개수.
 * @param argv 커맨드 라인 인자 배열.
 * @return 성공 시 0, 오류 시 1.
 */
int main(int argc, char* argv[]) {
    // --- Windows 콘솔 UTF-8 설정 (프로그램 시작 시) ---
#ifdef _WIN32
    try {
        // 콘솔 출력 코드 페이지를 UTF-8로 설정
        if (!SetConsoleOutputCP(CP_UTF8)) {
            std::cerr << "Warning: Failed to set console output codepage to UTF-8. Error code: " << GetLastError() << std::endl;
        }
        // 콘솔 입력 코드 페이지를 UTF-8로 설정
        if (!SetConsoleCP(CP_UTF8)) {
            std::cerr << "Warning: Failed to set console input codepage to UTF-8. Error code: " << GetLastError() << std::endl;
        }
        // C/C++ 표준 라이브러리 로케일을 UTF-8로 설정 (printf 등에도 영향)
        // 시스템에 ".UTF-8" 로케일이 설치되어 있어야 함
        // setlocale(LC_ALL, ".UTF-8"); // C 스타일
        std::locale::global(std::locale(".UTF-8")); ///< C++ 스타일 (iostream에 영향)
        std::cout.imbue(std::locale());
        std::cerr.imbue(std::locale());
        std::wcout.imbue(std::locale()); ///< 와이드 문자용
        std::wcerr.imbue(std::locale());
        fprintf(stdout, "Windows console UTF-8 settings applied.\n");
    }
    catch (const std::runtime_error& e) {
        // 로케일 설정 실패 시 경고 출력 (프로그램 중단은 아님)
        std::cerr << "Warning: Failed to set C++ global locale to .UTF-8: " << e.what() << std::endl;
    }
#endif
    // --- 콘솔 설정 끝 ---

    fprintf(stdout, "CherryRecorder Server v1.1 - WEBSOCKET_FIX_APPLIED\n");

    // --- io_context 생성 --- 
    // 모든 서버가 io_context를 공유하도록 변경 (더 효율적일 수 있음)
    const int num_total_threads = 4; // 예시: 전체 서버용 스레드 수
    net::io_context ioc{num_total_threads};

    // --- Boost.Asio signal_set 사용 --- 
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    // async_wait 호출을 서버 생성 *후* 로 이동
    // signals.async_wait(...);

    try {
        // --- 설정 값 읽기 (환경 변수 사용) ---
        unsigned short http_port = get_required_port_env_var("HTTP_PORT", 8080);
        std::string http_bind_ip = get_env_var("HTTP_BIND_IP", "0.0.0.0");
        int http_threads = get_int_env_var("HTTP_THREADS", 1); 
        unsigned short ws_port = get_required_port_env_var("WS_PORT", 33334);  // WebSocket 포트


        // --- 서버 객체 생성 (로컬 스마트 포인터 사용) ---
        auto http_server = std::make_unique<HttpServer>(http_bind_ip, http_port, http_threads);
        auto chat_server = std::make_shared<ChatServer>(ioc, ws_port);  // ChatServer를 여러 WebSocket 리스너가 공유
        std::shared_ptr<WebSocketListener> ws_listener; // ws_listener를 미리 선언

        // WS 리스너 생성 (비보안 WebSocket)
        fprintf(stdout, "Attempting to create WS listener...\n");
        ws_listener = std::make_shared<WebSocketListener>(
            ioc,
            net::ip::tcp::endpoint{net::ip::make_address("0.0.0.0"), ws_port},
            chat_server
        );
        fprintf(stdout, "WS listener created successfully.\n");
        
        // --- signal_set 핸들러 설정 (서버 객체 생성 후) ---
        signals.async_wait(
            [&ioc, &http_server, &chat_server, &ws_listener]
            (const beast::error_code& ec, int signal_number) {
                fprintf(stdout, "\nSignal %d received. Shutting down...\n", signal_number);

                // 각 서버의 stop() 메서드 호출 (람다 캡처 사용)
                if (http_server) {
                    fprintf(stdout, "Requesting HTTP server stop...\n");
                    http_server->stop();
                }
                if (chat_server) {
                    fprintf(stdout, "Requesting Chat server stop...\n");
                    chat_server->stop();
                }

                // 공유 io_context 중지 요청
                fprintf(stdout, "Requesting shared io_context stop...\n");
                ioc.stop(); 
            });
        // --- 시그널 설정 끝 ---
        
        // --- 서버 시작 ---
        http_server->run();
        // WebSocket 리스너 실행
        if (ws_listener) {
            fprintf(stdout, "Running WS listener...\n");
            ws_listener->run();
        }
        fprintf(stdout, "HTTP server starting on %s:%hu (%d threads)\n", http_bind_ip.c_str(), http_port, http_threads);
        fprintf(stdout, "WebSocket (WS) server starting on port %hu (using shared io_context)\n", ws_port);
        fprintf(stdout, "\n[알림] SSL/TLS는 nginx 또는 AWS ALB에서 처리합니다.\n");
        fprintf(stdout, "애플리케이션은 HTTP(port %hu)와 WebSocket(port %hu)만 제공합니다.\n", http_port, ws_port);

        // --- 공유 io_context 실행 스레드 시작 ---
        std::vector<std::thread> io_threads;
        io_threads.reserve(num_total_threads);
        fprintf(stdout, "Starting %d shared IO threads for Chat Server...\n", num_total_threads);
        for (int i = 0; i < num_total_threads; ++i) {
            io_threads.emplace_back([&ioc, i]() { 
                fprintf(stdout, "[Shared IO Thread %d] Starting io_context::run()...\n", i);
                try {
                    ioc.run(); 
                    fprintf(stdout, "[Shared IO Thread %d] io_context::run() finished.\n", i);
                } catch (const std::exception& e) {
                    fprintf(stderr, "[Shared IO Thread %d] Exception: %s\n", i, e.what());
                }
            });
        }

        fprintf(stdout, "All servers running. Press Ctrl+C or send SIGTERM to exit.\n");

        // --- 메인 스레드는 종료 시그널 대기 또는 다른 작업 수행 --- 
        // 시그널 핸들러가 stop()을 호출하고 io_context를 중지시킬 것임
        // 모든 스레드가 종료될 때까지 대기
        for(auto& t : io_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        // HttpServer의 자체 스레드 join은 해당 객체의 소멸자 또는 stop()에서 처리되어야 함
        // (HttpServer 구현 확인 필요)
        if (http_server) {
             // HttpServer의 stop()이 스레드 join을 보장하는지 확인 필요
             // 그렇지 않다면 여기서 명시적 join 필요할 수 있음
             // http_server->join_threads(); // 예시
        }

        fprintf(stdout, "Main thread exiting after IO threads finished.\n");

    }
    catch (const std::system_error& e) { 
        fprintf(stderr, "[Error] System error during server setup or execution: %s (code: %d)\n", e.what(), e.code().value());
        // 오류 발생 시 io_context 중지 시도 (이미 실행 중이라면)
        // if (!ioc.stopped()) ioc.stop(); // ioc 접근 문제
        return 1;
    }
    catch (const std::runtime_error& e) { 
        fprintf(stderr, "[Error] Runtime error during server setup: %s\n", e.what());
        // if (!ioc.stopped()) ioc.stop(); // ioc 접근 문제
        return 1;
    }
    catch (const std::exception& e) { 
        fprintf(stderr, "[Error] Unhandled standard exception in main: %s\n", e.what());
        // if (!ioc.stopped()) ioc.stop(); // ioc 접근 문제
        return 1;
    }
    catch (...) { 
        fprintf(stderr, "[Error] Unknown exception caught in main!\n");
        // if (!ioc.stopped()) ioc.stop(); // ioc 접근 문제
        return 1;
    }

    // 스마트 포인터가 범위를 벗어나면 자동으로 서버 객체 소멸 (reset 불필요)
    // g_http_server_ptr.reset(); 
    // g_echo_server_ptr.reset();
    // g_chat_server_ptr.reset();
    // g_shared_ioc_ptr = nullptr;

    fprintf(stdout, "Server application finished gracefully.\n");
    return 0; 
}