#include "../include/ChatServer.hpp"
#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <deque>
#include <mutex>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <boost/asio/read_until.hpp>

namespace net = boost::asio;
using tcp = net::ip::tcp;

/**
 * @brief ChatServer 테스트를 위한 Fixture 클래스.
 * @details 각 테스트 케이스 실행 전에 ChatServer 인스턴스를 생성 및 시작하고,
 *          실행 후에는 서버를 중지하고 리소스를 정리한다.
 *          테스트용 io_context와 실행 스레드를 관리한다.
 */
class ChatServerTest : public ::testing::Test {
protected:
    net::io_context server_ioc_;           ///< 서버용 io_context
    net::io_context client_ioc_;           ///< 클라이언트용 io_context
    std::thread server_thread_;            ///< 서버 io_context 실행 스레드
    std::thread client_thread_;            ///< 클라이언트 io_context 실행 스레드
    std::shared_ptr<ChatServer> server_;   ///< 테스트 대상 ChatServer 인스턴스
    unsigned short test_port_ = 0;         ///< 테스트에 사용할 포트 번호 (0으로 설정하면 시스템이 할당)
    std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> server_work_guard_;
    std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> client_work_guard_;

    /**
     * @brief 각 테스트 케이스 시작 전에 호출되는 설정 함수.
     * @details 서버와 클라이언트를 위한 별도의 io_context 스레드를 시작하고
     *          ChatServer를 생성 및 실행한다.
     */
    void SetUp() override {
        spdlog::info("Setting up test...");
        
        // 시스템이 할당할 수 있는 포트 찾기
        std::shared_ptr<tcp::acceptor> temp_acceptor;
        
        try {
            temp_acceptor = std::make_shared<tcp::acceptor>(server_ioc_);
            tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), 0);
            
            // 포트 0으로 바인드 -> 시스템이 사용 가능한 포트 할당
            boost::system::error_code ec;
            temp_acceptor->open(endpoint.protocol(), ec);
            if (ec) {
                FAIL() << "Failed to open acceptor: " << ec.message();
                return;
            }
            
            temp_acceptor->bind(endpoint, ec);
            if (ec) {
                FAIL() << "Failed to bind acceptor: " << ec.message();
                return;
            }
            
            // 할당된 포트 번호 가져오기
            test_port_ = temp_acceptor->local_endpoint().port();
            temp_acceptor->close();
            temp_acceptor.reset();
            
            spdlog::info("Allocated test port: {}", test_port_);
        } catch (const std::exception& e) {
            FAIL() << "Exception during port allocation: " << e.what();
            return;
        }
        
        // work guard 생성 - io_context가 작업이 없어도 실행 유지하도록
        server_work_guard_ = std::make_unique<net::executor_work_guard<net::io_context::executor_type>>(
            server_ioc_.get_executor());
        client_work_guard_ = std::make_unique<net::executor_work_guard<net::io_context::executor_type>>(
            client_ioc_.get_executor());
        
        // io_context 실행 전 서버 생성 (이전 버그의 원인)
        try {
            // 서버 생성 
            server_ = std::make_shared<ChatServer>(server_ioc_, test_port_);
            
            // 디버그 메시지 추가
            spdlog::info("Creating ChatServer on port {}", test_port_);
            
            // 서버 io_context 시작
            server_thread_ = std::thread([this]() { 
                try {
                    spdlog::info("Server io_context thread starting...");
                    server_ioc_.run(); 
                    spdlog::info("Server io_context thread complete.");
                } catch (const std::exception& e) {
                    spdlog::error("Server thread exception: {}", e.what());
                }
            });
            
            // 클라이언트 io_context 시작
            client_thread_ = std::thread([this]() { 
                try {
                    spdlog::info("Client io_context thread starting...");
                    client_ioc_.run(); 
                    spdlog::info("Client io_context thread complete.");
                } catch (const std::exception& e) {
                    spdlog::error("Client thread exception: {}", e.what());
                }
            });
            
            // 스레드가 시작할 시간 부여
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 이미 실행 중인 io_context에 서버 시작 작업 게시
            server_->run(); // run()은 리스너 시작 및 시그널 설정만 수행
            
            // 서버가 리슨 상태가 될 때까지 잠시 대기
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            spdlog::info("Server started successfully");
        } catch (const std::exception& e) {
             FAIL() << "Server setup failed: " << e.what();
        }
    }

    /**
     * @brief 각 테스트 케이스 종료 후에 호출되는 정리 함수.
     * @details ChatServer를 중지시키고, io_context들을 중지시킨 후 관련 스레드를 join한다.
     */
    void TearDown() override {
        spdlog::info("Tearing down test...");
        if (server_) {
            try {
                spdlog::info("Stopping server...");
                server_->stop(); 
                spdlog::info("Server stopped");
                
                // ★★★ Add significant delay before resetting server ★★★
                // Allow ample time for async operations (like leave callbacks) to complete.
                spdlog::info("Waiting 100 milliseconds before resetting server...");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                spdlog::info("Proceeding with server reset.");
                
                server_.reset();
                spdlog::info("Server reset");
            } catch (const std::exception& e) {
                spdlog::error("Exception during server shutdown: {}", e.what());
            } catch (...) {
                spdlog::error("Unknown exception during server shutdown");
            }
        }
        
        // work guard 해제하여 io_context가 자연스럽게 종료되도록 함
        try {
            spdlog::info("Releasing work guards...");
            client_work_guard_.reset();
            server_work_guard_.reset();
            spdlog::info("Work guards released");
        } catch (const std::exception& e) {
            spdlog::error("Exception during work guard release: {}", e.what());
        }
        
        // io_context 이벤트 루프 중지 (남은 작업이 있을 경우)
        try {
            spdlog::info("Stopping io_contexts...");
            client_ioc_.stop();
            server_ioc_.stop();
            spdlog::info("io_contexts stopped");
        } catch (const std::exception& e) {
            spdlog::error("Exception during io_context shutdown: {}", e.what());
        }
        
        // 스레드 종료 대기
        try {
            spdlog::info("Joining threads...");
            if (client_thread_.joinable()) {
                client_thread_.join();
            }
            if (server_thread_.joinable()) {
                server_thread_.join();
            }
            spdlog::info("Threads joined");
        } catch (const std::exception& e) {
            spdlog::error("Exception during thread join: {}", e.what());
        }
        
        // 포트가 완전히 해제될 시간을 위해 잠시 대기
        spdlog::info("Waiting for port release...");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        spdlog::info("TearDown complete");
    }
};

// --- TestClient 클래스 --- 
/**
 * @class TestClient
 * @brief ChatServer 테스트를 위한 간단한 비동기 클라이언트.
 * @details 서버에 연결하고, 메시지를 보내고, 서버로부터 비동기적으로 메시지를 받아 저장한다.
 *          특정 메시지 수신을 기다리는 기능을 제공하여 비동기 테스트를 용이하게 한다.
 */
class TestClient : public std::enable_shared_from_this<TestClient> {
    net::io_context& ioc_;             ///< @brief 클라이언트 비동기 작업을 위한 io_context 참조.
    tcp::socket socket_;               ///< @brief 서버와 통신하는 소켓.
    tcp::resolver resolver_;           ///< @brief 호스트 이름 및 서비스 이름을 엔드포인트로 변환.
    std::shared_ptr<net::streambuf> response_buf_;  ///< @brief 서버 응답 수신 버퍼 (shared_ptr로 관리).
    std::deque<std::string> received_messages_; ///< @brief 수신된 메시지 저장 큐.
    std::mutex mutex_;                 ///< @brief received_messages_ 접근 동기화를 위한 뮤텍스.
    std::condition_variable cv_;       ///< @brief 메시지 수신 또는 연결 종료를 기다리기 위한 조건 변수.
    std::atomic<bool> connected_{false}; ///< @brief 현재 연결 상태 플래그.
    std::atomic<bool> shutting_down_{false}; ///< @brief 종료 중 플래그 (비동기 작업 취소용).

    /** @brief 소켓을 닫고 상태를 업데이트하는 내부 함수. */
    void CloseInternal() {
        if (connected_.exchange(false)) { // 한 번만 실행되도록 보장
            // 종료 중 플래그 설정
            shutting_down_ = true;
            
            try {
                boost::system::error_code ignored_ec;
                // Shutdown은 상대방에게 종료를 알리는 역할, 실패할 수 있음
                if (socket_.is_open()) {
                    // Cancel pending async operations first
                    socket_.cancel(ignored_ec);
                    socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
                    // Close는 로컬 소켓 리소스 해제
                    socket_.close(ignored_ec);
                }
                cv_.notify_all(); // 연결 종료 알림
            } catch (const std::exception& e) {
                spdlog::error("[TestClient] Exception in CloseInternal: {}", e.what());
            }
        }
    }

    /** @brief 서버로부터 메시지를 비동기적으로 읽는 내부 함수. */
    void AsyncRead() {
        if (shutting_down_ || !connected_) return;

        auto self = shared_from_this(); // Keep alive during async operation
        // Check if buffer exists, re-create if necessary (shouldn't happen often)
        if (!response_buf_) {
            response_buf_ = std::make_shared<net::streambuf>();
            spdlog::warn("[TestClient] Response buffer was null, re-created.");
        }
        auto current_buffer = response_buf_; // Capture the buffer for the lambda

        // ★★★ Corrected: Call as free function ★★★
        boost::asio::async_read_until(socket_, *current_buffer, "\n",
            [this, self, current_buffer](boost::system::error_code ec, std::size_t bytes_transferred) {
                if (shutting_down_) return;

                if (!ec) {
                    try {
                        std::istream response_stream(current_buffer.get());
                        std::string line;
                        bool message_added = false;

                        // Process all complete lines in the buffer
                        while (std::getline(response_stream, line)) {
                            // Handle potential CRLF by removing trailing CR if present
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }
                            
                            // Log raw received line (before checking if empty)
                            // Use a different log level or conditional logging if too verbose
                            spdlog::trace("[TestClient] Raw line read: '{}'", line); 

                            if (!line.empty()) {
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    received_messages_.push_back(line);
                                    spdlog::info("[TestClient] Received: '{}'", line); // Use info for processed messages
                                }
                                message_added = true;
                            } else {
                                // Optionally log empty lines if necessary for debugging
                                spdlog::debug("[TestClient] Received empty line, ignoring.");
                            }
                        }
                         // Clear EOF state if getline stopped because it reached the end of the buffer
                         // without finding a newline, allowing subsequent reads to work correctly.
                         if (response_stream.eof()) {
                            response_stream.clear();
                         }

                        if (message_added) {
                            cv_.notify_all(); // Notify waiting threads about new messages
                        }

                        // Continue reading if the session is still active
                        if (!shutting_down_ && connected_) {
                           AsyncRead();
                        }

                    } catch (const std::exception& e) {
                         spdlog::error("[TestClient] Exception during read processing: {}", e.what());
                         CloseInternal(); // Stop session on processing error
                    }
                } else {
                    if (ec != net::error::eof && ec != net::error::operation_aborted && ec != net::error::connection_reset) {
                        spdlog::error("[TestClient] Read Error: {}", ec.message());
                    } else if (ec == net::error::eof) {
                        spdlog::info("[TestClient] Connection closed by peer (EOF).");
                    } else if (ec == net::error::connection_reset) {
                        spdlog::info("[TestClient] Connection reset by peer.");
                    } else { // operation_aborted
                        spdlog::info("[TestClient] Read operation aborted.");
                    }
                    CloseInternal(); // Initiate session cleanup on any read error or disconnect
                }
            }
        );
    }

public:
    /** @brief TestClient 생성자. */
    TestClient(net::io_context& ioc) 
        : ioc_(ioc), 
          socket_(ioc), 
          resolver_(ioc),
          response_buf_(std::make_shared<net::streambuf>()) 
    {}

    /** @brief TestClient 소멸자. */
    ~TestClient() { 
        shutting_down_ = true; 
        CloseInternal(); 
    }
    
    /** 
     * @brief 서버에 연결.
     * @param host 연결할 호스트 주소 (e.g., "127.0.0.1").
     * @param port 연결할 포트 번호.
     * @return 연결 성공 시 true, 실패 시 false.
     */
    bool Connect(const std::string& host, unsigned short port) {
        if (connected_) return true; // 이미 연결됨
        try {
            shutting_down_ = false; // 연결 시작 시 종료 플래그 초기화
            
            auto endpoints = resolver_.resolve(host, std::to_string(port));
            net::connect(socket_, endpoints);
            connected_ = true;
            
            socket_.set_option(tcp::no_delay(true));
            
            spdlog::info("[TestClient] Connected to {}:{}", host, port);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            AsyncRead(); // 연결 후 바로 읽기 시작
            return true;
        } catch (std::exception const& e) {
            spdlog::error("[TestClient] Connect Error: {}", e.what());
            connected_ = false;
            return false;
        }
    }

    /** 
     * @brief 서버에 메시지(명령어 포함) 전송.
     * @param message 전송할 문자열. 자동으로 끝에 개행 문자가 추가된다.
     */
    void Send(const std::string& message) {
        if (!connected_ || shutting_down_) {
             spdlog::warn("[TestClient] Send Error: Not connected or shutting down.");
             return;
        }
        
        auto safe_message_ptr = std::make_shared<std::string>(message);
        auto formatted_message_ptr = std::make_shared<std::string>(*safe_message_ptr + "\n");
        
        spdlog::info("[TestClient] Sending: '{}'", *safe_message_ptr);
        
        std::weak_ptr<TestClient> weak_self = shared_from_this();
        
        net::post(ioc_, [this, weak_self, formatted_message_ptr]() {
             auto self = weak_self.lock();
             if (!self) return;
             if (!connected_ || shutting_down_) return;
             
             try {
                 auto buffer_ptr = std::make_shared<net::const_buffer>(
                     formatted_message_ptr->c_str(), 
                     formatted_message_ptr->size()
                 );
                 
                 net::async_write(socket_, *buffer_ptr,
                    [this, weak_self, formatted_message_ptr, buffer_ptr](boost::system::error_code ec, std::size_t /*length*/) {
                        auto self = weak_self.lock();
                        if (!self) return;
                        
                        if (ec && connected_ && !shutting_down_) {
                             if (ec != net::error::operation_aborted) {
                                 spdlog::error("[TestClient] Send Error: {}", ec.message());
                             }
                             CloseInternal();
                        }
                    });
             } catch (const std::exception& e) {
                 spdlog::error("[TestClient] Exception in Send post lambda: {}", e.what());
                 CloseInternal();
             }
        });
    }

    /** @brief 클라이언트 연결 종료. */
    void Close() {
         shutting_down_ = true;
         net::post(ioc_, [this](){ 
             // Ensure CloseInternal is called safely from the io_context thread
             // Check if already closed to avoid redundant calls if Close is called multiple times
             if (connected_) { 
                 CloseInternal(); 
             } 
         });
    }

    /** 
     * @brief 특정 개수 이상의 메시지를 받거나 타임아웃될 때까지 대기.
     * @param count 기다릴 최소 메시지 개수 (현재까지 받은 총 메시지 기준).
     * @param timeout 대기 시간 (기본값 2000ms).
     * @return 성공 시 true (count개 이상 받음), 타임아웃 또는 연결 종료 시 false.
     */
    bool WaitForMessages(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        auto self = shared_from_this();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (received_messages_.size() >= count) {
                spdlog::debug("[TestClient] Already have {}/{} messages.", received_messages_.size(), count);
                return true;
            }
            if (!connected_) {
                spdlog::warn("[TestClient] WaitForMessages called but not connected.");
                return false;
            }
        }
        
        std::unique_lock<std::mutex> lock(mutex_);
        spdlog::debug("[TestClient] Waiting for {} messages (currently have {}), timeout: {}ms", 
                     count, received_messages_.size(), timeout.count());
                     
        bool success = cv_.wait_for(lock, timeout, [this, count]() { 
            // Check shutdown flag inside predicate
            return shutting_down_ || !connected_ || received_messages_.size() >= count; 
        });
        
        bool result = success && received_messages_.size() >= count && connected_ && !shutting_down_;
        spdlog::debug("[TestClient] Wait finished. Success: {}, Messages: {}, Connected: {}, ShuttingDown: {}. Final Result: {}", 
                     success, received_messages_.size(), connected_.load(), shutting_down_.load(), result);
                     
        return result;
    }

    /** 
     * @brief 받은 메시지 목록 반환 (읽기 전용 복사본).
     * @return 수신된 메시지들의 `std::vector<std::string>`.
     */
    std::vector<std::string> GetMessages() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> copy(received_messages_.begin(), received_messages_.end());
        return copy;
    }

    /** 
     * @brief 마지막으로 받은 메시지 반환. 없으면 빈 문자열.
     * @return 마지막 메시지 문자열.
     */
    std::string GetLastMessage() {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_messages_.empty() ? "" : received_messages_.back();
    }
     
    /** 
     * @brief 받은 메시지 중 특정 부분 문자열을 포함하는 메시지가 있는지 확인.
     * @param substring 찾을 부분 문자열.
     * @return 포함하는 메시지가 있으면 true, 없으면 false.
     */
     bool HasReceived(const std::string& substring) {
         std::lock_guard<std::mutex> lock(mutex_);
         return std::any_of(received_messages_.begin(), received_messages_.end(),
                            [&](const std::string& msg){ return msg.find(substring) != std::string::npos; });
     }

     /** @brief 받은 메시지 큐를 비운다. */
     void ClearMessages() {
         std::lock_guard<std::mutex> lock(mutex_);
         received_messages_.clear();
     }

    // NEW: Waits for a message containing a specific substring
    bool WaitForSpecificMessage(const std::string& substring, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        auto self = shared_from_this(); // Keep alive
        auto check_condition = [&]() {
            if (shutting_down_ || !connected_) return true; // Stop waiting if disconnected/shutting down
            return std::any_of(received_messages_.begin(), received_messages_.end(),
                               [&](const std::string& msg){ return msg.find(substring) != std::string::npos; });
        };

        std::unique_lock<std::mutex> lock(mutex_);
        spdlog::debug("[TestClient] Waiting for message containing '{}', timeout: {}ms", substring, timeout.count());

        // Check if already present before waiting
        if (check_condition()) {
             spdlog::debug("[TestClient] Substring '{}' already found.", substring);
             // Ensure we return false if the condition met because we disconnected
             return connected_ && !shutting_down_ && std::any_of(received_messages_.begin(), received_messages_.end(),
                               [&](const std::string& msg){ return msg.find(substring) != std::string::npos; });
        }

        bool success = cv_.wait_for(lock, timeout, check_condition);

        bool found = success && connected_ && !shutting_down_ && 
                     std::any_of(received_messages_.begin(), received_messages_.end(),
                               [&](const std::string& msg){ return msg.find(substring) != std::string::npos; });
                               
        spdlog::debug("[TestClient] Specific wait finished. Found '{}': {}, Success: {}, Connected: {}, ShuttingDown: {}", 
                     substring, found, success, connected_.load(), shutting_down_.load());
        return found;
    }
};


// --- 테스트 케이스 --- 

/**
 * @test BasicConnectionAndWelcome
 * @brief 클라이언트가 서버에 성공적으로 연결하고 환영 메시지를 받는지 테스트한다.
 */
TEST_F(ChatServerTest, BasicConnectionAndWelcome) {
    auto client = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client->WaitForMessages(5, std::chrono::milliseconds(3000))) << "Did not receive enough initial messages";
    EXPECT_TRUE(client->HasReceived("Welcome to the CherryRecorder Chat Server!"));
    EXPECT_TRUE(client->HasReceived("Your temporary ID is:"));
    EXPECT_TRUE(client->HasReceived("Please set your nickname using /nick <nickname>"));
    EXPECT_TRUE(client->HasReceived("Enter /help for a list of commands."));
    EXPECT_TRUE(client->HasReceived("Enter /join <roomname> to join or create a room."));
    client->Close();
}

/**
 * @test SetValidNickname
 * @brief 유효한 닉네임을 설정하고 성공 응답 및 전역 알림을 받는지 테스트한다.
 */
TEST_F(ChatServerTest, SetValidNickname) {
    auto client1 = std::make_shared<TestClient>(client_ioc_);
    auto client2 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client2->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client1->WaitForMessages(5, std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForMessages(5, std::chrono::milliseconds(2000)));

    client1->ClearMessages();
    client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string nick_cmd = "/nick testuser";
    std::string expected_success_c1 = "닉네임이 'testuser'(으)로 변경되었습니다.";
    std::string expected_broadcast_c2 = "사용자 'testuser'님이 입장했습니다.";

    client1->Send(nick_cmd);

    ASSERT_TRUE(client1->WaitForSpecificMessage(expected_success_c1, std::chrono::milliseconds(2000)))
        << "C1: Did not receive specific success message for nick change.";

    ASSERT_TRUE(client2->WaitForSpecificMessage(expected_broadcast_c2, std::chrono::milliseconds(2000)))
        << "C2: Did not receive specific broadcast message for nick change.";

    client1->Close();
    client2->Close();
}

/**
 * @test SetDuplicateNickname
 * @brief 다른 클라이언트가 이미 사용 중인 닉네임 설정을 시도하고 에러 응답을 받는지 테스트한다.
 */
TEST_F(ChatServerTest, SetDuplicateNickname) {
    auto client1 = std::make_shared<TestClient>(client_ioc_);
    auto client2 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client2->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client1->WaitForMessages(5, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(client2->WaitForMessages(5, std::chrono::milliseconds(10000)));

    client1->ClearMessages();
    client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    spdlog::info("===== 첫 번째 클라이언트 닉네임 시도 =====");
    client1->Send("/nick dup_nick");
    ASSERT_TRUE(client1->WaitForSpecificMessage("닉네임이 'dup_nick'(으)로 변경되었습니다.", std::chrono::milliseconds(10000)))
        << "Client1 did not receive success for nick change";
    spdlog::info("Client1 받은 메시지 수: {}", client1->GetMessages().size());
    spdlog::info("===== client2가 입장 알림 확인 =====");
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'dup_nick'님이 입장했습니다.", std::chrono::milliseconds(10000)))
        << "Client2 did not receive broadcast for nick change";
    spdlog::info("Client2 받은 메시지 수: {}", client2->GetMessages().size());
    client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("===== 두 번째 클라이언트 중복 닉네임 시도 =====");
    client2->Send("/nick dup_nick");
    ASSERT_TRUE(client2->WaitForSpecificMessage("Error: 닉네임 'dup_nick'은(는) 이미 사용 중", std::chrono::milliseconds(10000)))
        << "Client2 did not receive error for duplicate nickname";
    spdlog::info("Client2 중복 시도 후 받은 메시지 수: {}", client2->GetMessages().size());
    spdlog::info("===== 테스트 정상 종료 - 연결 닫기 =====");
    client1->Close();
    client2->Close();
}

/**
 * @test SetInvalidNickname
 * @brief 유효하지 않은 닉네임(공백 포함) 설정을 시도하고 에러 응답을 받는지 테스트한다.
 */
TEST_F(ChatServerTest, SetInvalidNickname) {
    auto client = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client->WaitForMessages(5));
    client->ClearMessages();

    client->Send("/nick invalid name");
    ASSERT_TRUE(client->WaitForSpecificMessage("Error: 닉네임에 공백 문자를 포함할 수 없습니다", std::chrono::milliseconds(1000)))
        << "Client did not receive error for invalid nickname (space)";

    client->ClearMessages();
    client->Send("/nick toolongnicknameistoolong");
    ASSERT_TRUE(client->WaitForSpecificMessage("Error: 닉네임은 20자를 초과할 수 없습니다", std::chrono::milliseconds(1000)))
        << "Client did not receive error for invalid nickname (long)";

    client->Close();
}

/**
 * @test JoinAndLeaveRoom
 * @brief 채팅방에 참여하고 퇴장하는 기본 흐름과 관련 알림을 테스트한다.
 */
TEST_F(ChatServerTest, JoinAndLeaveRoom) {
    auto client1 = std::make_shared<TestClient>(client_ioc_);
    auto client2 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client2->Connect("127.0.0.1", test_port_));
    // Initial join messages removed - only sent after nickname is set
    ASSERT_TRUE(client1->WaitForMessages(5, std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForMessages(5, std::chrono::milliseconds(2000)));

    client1->Send("/nick user1");
    client2->Send("/nick user2");
    ASSERT_TRUE(client1->WaitForSpecificMessage("닉네임이 'user1'(으)로 변경되었습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'user1'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("닉네임이 'user2'(으)로 변경되었습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'user2'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'user2'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'user1'님이 입장했습니다.", std::chrono::milliseconds(2000)));

    client1->ClearMessages(); client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client1->Send("/join testroom");
    ASSERT_TRUE(client1->WaitForSpecificMessage("testroom' 방에 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("현재 멤버 (1): user1 (You)", std::chrono::milliseconds(1000)));

    client2->Send("/join testroom");
    ASSERT_TRUE(client2->WaitForSpecificMessage("testroom' 방에 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("현재 멤버 (2):", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("user1", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("user2 (You)", std::chrono::milliseconds(1000)));

    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'user2'님이 방에 들어왔습니다.", std::chrono::milliseconds(1000)));

    client1->ClearMessages(); client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client2->Send("/leave");
    ASSERT_TRUE(client2->WaitForSpecificMessage("방에서 퇴장했습니다.", std::chrono::milliseconds(1000)));

    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'user2'님이 'testroom' 방에서 나갔습니다.", std::chrono::milliseconds(1000)));

    client1->Close();
    client2->Close();
}

/**
 * @test SendMessageInRoom
 * @brief 두 사용자가 같은 방에 있을 때 한 사용자가 보낸 메시지가 다른 사용자에게 전달되는지 테스트
 */
TEST_F(ChatServerTest, SendMessageInRoom) {
    auto client1 = std::make_shared<TestClient>(client_ioc_);
    auto client2 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client2->Connect("127.0.0.1", test_port_));
    // Initial join messages removed - only sent after nickname is set
    ASSERT_TRUE(client1->WaitForMessages(5, std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForMessages(5, std::chrono::milliseconds(2000)));

    spdlog::info("===== 환영 메시지 수신 완료 =====");
    client1->Send("/nick user1"); client2->Send("/nick user2");
    ASSERT_TRUE(client1->WaitForSpecificMessage("닉네임이 'user1'", std::chrono::milliseconds(10000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("닉네임이 'user2'", std::chrono::milliseconds(10000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'user1'님이 입장했습니다.", std::chrono::milliseconds(10000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'user2'님이 입장했습니다.", std::chrono::milliseconds(10000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'user2'님이 입장했습니다.", std::chrono::milliseconds(10000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'user1'님이 입장했습니다.", std::chrono::milliseconds(10000)));

    client1->ClearMessages(); client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("===== 방 참여 시작 =====");
    client1->Send("/join testroom");
    ASSERT_TRUE(client1->WaitForSpecificMessage("testroom' 방에 입장했습니다.", std::chrono::milliseconds(10000)));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client2->Send("/join testroom");
    ASSERT_TRUE(client2->WaitForSpecificMessage("testroom' 방에 입장했습니다.", std::chrono::milliseconds(10000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'user2'님이 방에 들어왔습니다.", std::chrono::milliseconds(10000)));

    client1->ClearMessages(); client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("===== 방 내 메시지 전송 테스트 =====");
    std::string test_message = "Hello from user1 in testroom";
    std::string expected_msg_format = "[user1 @ testroom]: " + test_message;
    client1->Send(test_message);

    ASSERT_TRUE(client2->WaitForSpecificMessage(expected_msg_format, std::chrono::milliseconds(10000)))
        << "Client2 did not receive message in room. Last: " << client2->GetLastMessage();

    spdlog::info("===== 테스트 종료 - 연결 닫기 =====");
    client1->Close();
    client2->Close();
}

/**
 * @test SendMessageWithoutRoom
 * @brief 방에 참여하지 않은 상태에서 메시지를 보내고 전역 브로드캐스트되는지 테스트한다.
 */
TEST_F(ChatServerTest, SendMessageWithoutRoom) {
    auto client1 = std::make_shared<TestClient>(client_ioc_);
    auto client2 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client2->Connect("127.0.0.1", test_port_));
    // Initial join messages removed - only sent after nickname is set
    ASSERT_TRUE(client1->WaitForMessages(5, std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForMessages(5, std::chrono::milliseconds(2000)));

    client1->Send("/nick sender");
    client2->Send("/nick receiver");
    ASSERT_TRUE(client1->WaitForSpecificMessage("닉네임이 'sender'", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("닉네임이 'receiver'", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'sender'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'receiver'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'receiver'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'sender'님이 입장했습니다.", std::chrono::milliseconds(2000)));

    client1->ClearMessages();
    client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string message_content = "Global message!";
    std::string expected_format = "[sender]: " + message_content;
    client1->Send(message_content);

    ASSERT_TRUE(client2->WaitForSpecificMessage(expected_format, std::chrono::milliseconds(1000)))
        << "Receiver did not receive global message. Last: " << client2->GetLastMessage();

    client1->Close(); client2->Close();
}

/**
 * @test UserList
 * @brief 여러 클라이언트가 연결하고 닉네임을 설정한 후 /users 명령어로 목록을 확인한다.
 */
TEST_F(ChatServerTest, UserList) {
    auto c1 = std::make_shared<TestClient>(client_ioc_);
    auto c2 = std::make_shared<TestClient>(client_ioc_);
    auto c3 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(c1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(c2->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(c3->Connect("127.0.0.1", test_port_));
    // Initial join messages removed - only sent after nickname is set
    ASSERT_TRUE(c1->WaitForMessages(5, std::chrono::milliseconds(1000)));
    ASSERT_TRUE(c2->WaitForMessages(5, std::chrono::milliseconds(1000)));
    ASSERT_TRUE(c3->WaitForMessages(5, std::chrono::milliseconds(1000)));

    c1->Send("/nick Alice"); c2->Send("/nick Bob"); c3->Send("/nick Charlie");
    ASSERT_TRUE(c1->WaitForSpecificMessage("닉네임이 'Alice'", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(c1->WaitForSpecificMessage("사용자 'Alice'님이 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(c1->WaitForSpecificMessage("사용자 'Bob'님이 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(c1->WaitForSpecificMessage("사용자 'Charlie'님이 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(c2->WaitForSpecificMessage("닉네임이 'Bob'", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(c3->WaitForSpecificMessage("닉네임이 'Charlie'", std::chrono::milliseconds(1000)));
    
    c1->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    c1->Send("/users");

    ASSERT_TRUE(c1->WaitForSpecificMessage("현재 접속 중인 사용자", std::chrono::milliseconds(500)))
        << "Did not receive user list header";
    ASSERT_TRUE(c1->WaitForSpecificMessage("Alice (You)", std::chrono::milliseconds(500)))
        << "Did not receive own nickname in list";
    ASSERT_TRUE(c1->WaitForSpecificMessage("Bob", std::chrono::milliseconds(500)))
        << "Did not receive Bob in list";
    ASSERT_TRUE(c1->WaitForSpecificMessage("Charlie", std::chrono::milliseconds(500)))
        << "Did not receive Charlie in list";

    c1->Close(); c2->Close(); c3->Close();
}

/**
 * @test TestHelpCommand
 * @brief /help 명령어 입력 시 예상되는 도움말 메시지들을 수신하는지 테스트한다.
 */
TEST_F(ChatServerTest, TestHelpCommand) {
    auto client = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client->WaitForMessages(5));
    client->ClearMessages();

    client->Send("/help");

    ASSERT_TRUE(client->WaitForSpecificMessage("--- 도움말 ---", std::chrono::milliseconds(500)));
    ASSERT_TRUE(client->WaitForSpecificMessage("/nick <닉네임>", std::chrono::milliseconds(500)));
    ASSERT_TRUE(client->WaitForSpecificMessage("/join <방이름>", std::chrono::milliseconds(500)));
    ASSERT_TRUE(client->WaitForSpecificMessage("/leave", std::chrono::milliseconds(500)));
    ASSERT_TRUE(client->WaitForSpecificMessage("/users", std::chrono::milliseconds(500)));
    ASSERT_TRUE(client->WaitForSpecificMessage("/quit", std::chrono::milliseconds(500)));
    ASSERT_TRUE(client->WaitForSpecificMessage("/help", std::chrono::milliseconds(500)));
    ASSERT_TRUE(client->WaitForSpecificMessage("-------------", std::chrono::milliseconds(500)));

    client->Close();
}

/**
 * @test TestLeaveWhenNotInRoom
 * @brief 방에 참여하지 않은 상태에서 /leave 입력 시 적절한 메시지를 받는지 확인한다.
 */
TEST_F(ChatServerTest, TestLeaveWhenNotInRoom) {
    auto client = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client->WaitForMessages(5));
    client->Send("/nick leaver");
    ASSERT_TRUE(client->WaitForSpecificMessage("닉네임이 'leaver'", std::chrono::milliseconds(1000)));
    client->ClearMessages();

    client->Send("/leave");
    ASSERT_TRUE(client->WaitForSpecificMessage("Error: 현재 어떤 방에도 없습니다.", std::chrono::milliseconds(500)));

    client->Close();
}

/**
 * @test TestInvalidRoomName
 * @brief 유효하지 않은 방 이름(공백 포함)으로 /join 시도 시 에러 메시지를 받는지 확인한다.
 */
TEST_F(ChatServerTest, TestInvalidRoomName) {
    auto client = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client->WaitForMessages(5));
    client->Send("/nick joiner");
    ASSERT_TRUE(client->WaitForSpecificMessage("닉네임이 'joiner'", std::chrono::milliseconds(1000)));
    client->ClearMessages();

    client->Send("/join invalid room name");
    ASSERT_TRUE(client->WaitForSpecificMessage("Error: 방 이름에 공백 문자를 포함할 수 없습니다", std::chrono::milliseconds(500)));

    client->Close();
}

/**
 * @test TestQuitCommand
 * @brief 클라이언트가 /quit 입력 시 다른 클라이언트가 퇴장 알림을 받는지 확인한다.
 */
TEST_F(ChatServerTest, TestQuitCommand) {
    auto client1 = std::make_shared<TestClient>(client_ioc_);
    auto client2 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client2->Connect("127.0.0.1", test_port_));
    // Initial join messages removed - only sent after nickname is set
    ASSERT_TRUE(client1->WaitForMessages(5, std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForMessages(5, std::chrono::milliseconds(2000)));
    client1->Send("/nick quitter");
    client2->Send("/nick observer");
    ASSERT_TRUE(client1->WaitForSpecificMessage("닉네임이 'quitter'", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("닉네임이 'observer'", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'quitter'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'observer'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'observer'님이 입장했습니다.", std::chrono::milliseconds(2000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'quitter'님이 입장했습니다.", std::chrono::milliseconds(2000)));

    client1->ClearMessages();
    client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client1->Send("/quit");
    // Add a short delay to allow the server to process the quit message and broadcast
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Adjust delay if needed

    ASSERT_TRUE(client2->WaitForSpecificMessage("* 사용자 'quitter'님이 퇴장했습니다.", std::chrono::milliseconds(2000)))
        << "Observer did not receive quit broadcast. Last: " << client2->GetLastMessage();

    client2->Close();
}

/**
 * @test TestAbruptDisconnect
 * @brief 클라이언트가 갑자기 연결을 끊었을 때 다른 클라이언트가 퇴장 알림을 받는지 확인한다.
 */
TEST_F(ChatServerTest, TestAbruptDisconnect) {
    auto client1 = std::make_shared<TestClient>(client_ioc_);
    auto client2 = std::make_shared<TestClient>(client_ioc_);
    ASSERT_TRUE(client1->Connect("127.0.0.1", test_port_));
    ASSERT_TRUE(client2->Connect("127.0.0.1", test_port_));
    // Initial join messages removed - only sent after nickname is set
    ASSERT_TRUE(client1->WaitForMessages(5, std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client2->WaitForMessages(5, std::chrono::milliseconds(1000)));
    client1->Send("/nick dropper"); 
    client2->Send("/nick observer2");
    ASSERT_TRUE(client1->WaitForSpecificMessage("닉네임이 'dropper'", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("닉네임이 'observer2'", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'dropper'님이 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'observer2'님이 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client1->WaitForSpecificMessage("사용자 'observer2'님이 입장했습니다.", std::chrono::milliseconds(1000)));
    ASSERT_TRUE(client2->WaitForSpecificMessage("사용자 'dropper'님이 입장했습니다.", std::chrono::milliseconds(1000)));
    client2->ClearMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client1->Close();

    ASSERT_TRUE(client2->WaitForSpecificMessage("* 사용자 'dropper'님이 퇴장했습니다.", std::chrono::milliseconds(2000)))
        << "Observer did not receive disconnect broadcast. Last: " << client2->GetLastMessage();

    client2->Close();
}
