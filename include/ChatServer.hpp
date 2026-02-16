/**
 * @file ChatServer.hpp
 * @brief 채팅 서버의 핵심 로직을 담당하는 `ChatServer` 클래스를 정의합니다.
 * @details 이 클래스는 세션 관리, 채팅방 관리, 메시지 브로드캐스팅 등
 *          채팅 서버의 주요 기능을 총괄합니다. Boost.Asio를 사용하여 비동기적으로 동작하며,
 *          여러 클라이언트의 동시 접속을 처리합니다.
 */
#pragma once

// Standard Library Includes first
#include <memory>
#include <string>
#include <set>
#include <map> 
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>
#include <future>

// Boost Includes
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core/error.hpp>

// Project includes
#include "SessionInterface.hpp"

// Forward declarations
// class ChatSession; // 이제 필요 없음
class ChatServer;
// class ChatListener; // TCP 리스너 제거됨
class UserAccount;
class FileTransferInfo;
class MessageHistory;
class ChatRoom;

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
namespace beast = boost::beast;

/**
 * @class UserAccount
 * @brief 사용자 계정 정보를 저장하는 클래스.
 * 
 * 사용자 인증 및 권한 관리를 위한 정보를 저장한다.
 */
class UserAccount {
private:
    std::string username_;     ///< 사용자명
    std::string password_hash_; ///< 비밀번호 해시
    bool is_admin_ = false;    ///< 관리자 여부
    std::string last_ip_;      ///< 마지막 접속 IP
    std::string last_login_;   ///< 마지막 로그인 시간

public:
    /**
     * @brief UserAccount 생성자.
     * @param username 사용자명.
     * @param password_hash 비밀번호 해시.
     * @param is_admin 관리자 여부.
     */
    UserAccount(const std::string& username, 
                const std::string& password_hash, 
                bool is_admin = false);
    
    /**
     * @brief 사용자 비밀번호 확인.
     * @param password_hash 확인할 비밀번호 해시.
     * @return 일치하면 true.
     */
    bool check_password(const std::string& password_hash) const;
    
    /**
     * @brief 비밀번호 변경.
     * @param new_password_hash 새 비밀번호 해시.
     */
    void set_password(const std::string& new_password_hash);
    
    /**
     * @brief 관리자 권한 설정.
     * @param is_admin 관리자 여부.
     */
    void set_admin(bool is_admin);
    
    /**
     * @brief 관리자 여부 확인.
     * @return 관리자이면 true.
     */
    bool is_admin() const { return is_admin_; }
    
    /**
     * @brief 로그인 정보 업데이트.
     * @param ip 접속 IP.
     * @param login_time 로그인 시간.
     */
    void update_login_info(const std::string& ip, const std::string& login_time);
    
    /**
     * @brief 사용자명 반환.
     * @return 사용자명.
     */
    const std::string& username() const { return username_; }
};

/**
 * @class FileTransferInfo
 * @brief 파일 전송 정보를 저장하는 클래스.
 * 
 * 파일 전송 상태, 진행률 등의 정보를 관리한다.
 */
class FileTransferInfo {
public:
    enum class Status {
        Pending,    ///< 전송 대기 중
        InProgress, ///< 전송 중
        Completed,  ///< 전송 완료
        Failed,     ///< 전송 실패
        Rejected    ///< 수신자가 거부함
    };
    
private:
    std::string id_;              ///< 전송 ID
    std::string filename_;        ///< 파일명
    size_t filesize_;             ///< 파일 크기
    std::shared_ptr<SessionInterface> sender_;   ///< 전송자
    std::shared_ptr<SessionInterface> receiver_; ///< 수신자
    Status status_ = Status::Pending;       ///< 전송 상태
    size_t bytes_transferred_ = 0;          ///< 전송된 바이트 수
    std::string temp_path_;       ///< 임시 파일 경로
    
public:
    /**
     * @brief FileTransferInfo 생성자.
     * @param id 전송 ID.
     * @param filename 파일명.
     * @param filesize 파일 크기.
     * @param sender 전송자.
     * @param receiver 수신자.
     */
    FileTransferInfo(const std::string& id,
                    const std::string& filename,
                    size_t filesize,
                    std::shared_ptr<SessionInterface> sender,
                    std::shared_ptr<SessionInterface> receiver);
    
    /**
     * @brief 전송 ID 반환.
     * @return 전송 ID.
     */
    const std::string& id() const { return id_; }
    
    /**
     * @brief 파일명 반환.
     * @return 파일명.
     */
    const std::string& filename() const { return filename_; }
    
    /**
     * @brief 파일 크기 반환.
     * @return 파일 크기.
     */
    size_t filesize() const { return filesize_; }
    
    /**
     * @brief 전송자 반환.
     * @return 전송자.
     */
    std::shared_ptr<SessionInterface> sender() const { return sender_; }
    
    /**
     * @brief 수신자 반환.
     * @return 수신자.
     */
    std::shared_ptr<SessionInterface> receiver() const { return receiver_; }
    
    /**
     * @brief 전송 상태 반환.
     * @return 전송 상태.
     */
    Status status() const { return status_; }
    
    /**
     * @brief 전송 상태 설정.
     * @param status 새 상태.
     */
    void set_status(Status status) { status_ = status; }
    
    /**
     * @brief 전송된 바이트 수 반환.
     * @return 전송된 바이트 수.
     */
    size_t bytes_transferred() const { return bytes_transferred_; }
    
    /**
     * @brief 전송된 바이트 수 업데이트.
     * @param bytes 전송된 바이트 수.
     */
    void update_bytes_transferred(size_t bytes) { bytes_transferred_ = bytes; }
    
    /**
     * @brief 전송 진행률 반환.
     * @return 진행률 (0.0 ~ 1.0).
     */
    double progress() const { return static_cast<double>(bytes_transferred_) / filesize_; }
    
    /**
     * @brief 임시 파일 경로 설정.
     * @param path 임시 파일 경로.
     */
    void set_temp_path(const std::string& path) { temp_path_ = path; }
    
    /**
     * @brief 임시 파일 경로 반환.
     * @return 임시 파일 경로.
     */
    const std::string& temp_path() const { return temp_path_; }
};

/**
 * @class ChatServer
 * @brief TCP/WebSocket 기반 채팅 서버의 메인 클래스.
 * @details 클라이언트 세션(`ChatSession`, `WebSocketSession` 등)들을 관리하고,
 *          채팅방을 생성/제거하며, 사용자 간의 메시지를 중계하는 역할을 합니다.
 *          서버의 생명주기 관리, 시그널 처리, 비동기 작업 조율을 담당합니다.
 *          모든 동시성 문제는 `strand_`를 통해 관리하여 데이터 레이스를 방지합니다.
 */
class ChatServer : public std::enable_shared_from_this<ChatServer> {
private:
    // 서버 기본 자원
    net::io_context& ioc_;                ///< 서버가 사용할 io_context 참조
    unsigned short port_;                 ///< 서버가 수신할 포트 번호 (WebSocket 리스너에서 사용)
    net::signal_set signals_;             ///< 종료 시그널(SIGINT, SIGTERM) 처리용
    // std::shared_ptr<ChatListener> listener_ = nullptr; ///< TCP 리스너 제거됨
    
    // 세션 관리
    std::set<SessionPtr> sessions_; ///< 현재 연결된 모든 세션 (TCP 및 WebSocket)
    std::map<std::string, std::weak_ptr<SessionInterface>> nicknames_; ///< 닉네임 -> 세션 맵
    
    // 채팅방 관리
    std::map<std::string, std::shared_ptr<ChatRoom>> rooms_;  ///< 채팅방 이름 -> 채팅방 객체 맵
    std::mutex rooms_mutex_;              ///< 채팅방 컬렉션 보호 뮤텍스
    
    // 사용자 및 파일 전송 관리
    std::unordered_map<std::string, std::shared_ptr<UserAccount>> users_; ///< 사용자 계정 정보
    std::unordered_map<std::string, std::shared_ptr<FileTransferInfo>> file_transfers_; ///< 진행 중인 파일 전송 작업
    
    // 스레드 안전 및 동시성 제어
    net::strand<net::io_context::executor_type> strand_; ///< 서버 상태 접근 직렬화를 위한 스트랜드
    std::mutex nicknames_mutex_; ///< Mutex for protecting access to nicknames_ map (mutable for const methods if needed)
    std::mutex sessions_mutex_;  ///< Mutex for protecting access to sessions_ set (if strand isn't sufficient)
    
    // 설정 및 히스토리
    std::string config_file_;             ///< 설정 파일 경로 
    std::string history_dir_;             ///< 히스토리 저장 디렉토리
    std::unique_ptr<MessageHistory> history_; ///< 메시지 히스토리 관리자
    
    // 상태 플래그
    std::atomic<bool> stopped_{false};    ///< 서버 중지 상태 플래그 (원자적 접근)
    bool require_auth_ = false;           ///< 사용자 인증 필요 여부
    
    // Variables for shutdown synchronization
    std::atomic<size_t> stopping_sessions_counter_{0}; 
    std::optional<std::promise<void>> all_sessions_stopped_promise_{std::nullopt};

public:
    /**
     * @brief ChatServer 생성자 (io_context 주입 방식).
     * @param ioc 서버가 사용할 io_context 참조.
     * @param port 리슨할 포트 번호.
     * @param config_file 설정 파일 경로.
     * @param history_dir 히스토리 저장 디렉토리.
     */
    ChatServer(net::io_context& ioc, 
              unsigned short port, 
              const std::string& config_file = "chat_server.cfg",
              const std::string& history_dir = "history");
    
    /** 
     * @brief ChatServer 소멸자.
     * @details 서버가 실행 중이라면 stop()을 호출하여 안전하게 종료한다.
     */
    ~ChatServer();

    /** 
     * @brief 서버 시작.
     * @details 리스너를 생성 및 시작하고, 시그널 처리를 설정한다.
     *          io_context 실행은 외부에서 담당한다.
     */
    void run();

    /** 
     * @brief 서버 중지 요청.
     * @details 리스너를 중지하고 관련 리소스를 정리한다.
     *          io_context 중지는 외부에서 담당한다.
     */
    void stop();

    /** 
     * @brief 설정 파일 로드.
     * @param config_file_ 로 지정된 경로에서 설정을 로드한다. (구현 필요)
     * @return 성공 시 true, 실패 시 false.
     */
    bool load_config();
    
    /**
     * @brief 현재 설정을 파일에 저장.
     * @param config_file_ 로 지정된 경로에 현재 설정을 저장한다. (구현 필요)
     * @return 성공 시 true, 실패 시 false.
     */
    bool save_config();
    
    /**
     * @brief 사용자 계정 정보 로드.
     * @details 사용자 계정 파일에서 정보를 로드하여 `users_` 멤버를 채운다. (구현 필요)
     * @return 성공 시 true, 실패 시 false.
     */
    bool load_users();
    
    /**
     * @brief 현재 사용자 계정 정보를 파일에 저장.
     * @details `users_` 멤버의 정보를 사용자 계정 파일에 저장한다. (구현 필요)
     * @return 성공 시 true, 실패 시 false.
     */
    bool save_users();

    /** 
     * @brief 새 클라이언트 세션 등록.
     * @param session 등록할 세션의 `shared_ptr`.
     * @details 동기화 보호 하에 `sessions_` 목록에 새 세션을 추가한다.
     */
    void join(SessionPtr session);

    /** 
     * @brief 클라이언트 세션 제거.
     * @param session 제거할 세션의 `shared_ptr`.
     * @details 세션이 참여 중인 모든 방에서 나가도록 처리하고(`leave_all_rooms` 호출),
     *          동기화 보호 하에 `sessions_` 및 `nicknames_` 목록에서 세션을 안전하게 제거한다.
     */
    void leave(SessionPtr session);

    /**
     * @brief 모든 클라이언트에게 메시지 브로드캐스트 (전역).
     * @param message 전송할 메시지.
     * @param sender 메시지를 보낸 세션 (`shared_ptr`, 해당 세션에는 보내지 않음).
     * @details 동기화 보호 하에 `sessions_` 목록의 모든 활성 세션(sender 제외)에 메시지를 전달한다.
     *          내부적으로 `broadcast_impl`을 호출한다.
     */
    void broadcast(const std::string& message, SessionPtr sender);
    
    /**
     * @brief 개인 메시지 전송 (구현 필요).
     * @param message 전송할 메시지.
     * @param sender 보낸 세션 (`shared_ptr`).
     * @param receiver_nick 받는 사람 닉네임.
     * @return 성공 시 true (수신자 존재 및 메시지 전달 성공), 실패 시 false.
     */
    bool send_private_message(const std::string& message, 
                             SessionPtr sender, 
                             const std::string& receiver_nick);

    /** 
     * @brief 닉네임 중복 여부 확인 및 등록 시도 (비동기 방식).
     * @param nickname 등록할 닉네임.
     * @param session 해당 닉네임을 사용할 세션 (`shared_ptr`).
     * @param handler 작업 완료 시 호출될 콜백 함수. `void(bool success)` 형태.
     * @details 닉네임 유효성 검사 후, Strand 위에서 중복 확인 및 등록을 시도하고, 
     *          결과(성공/실패)를 콜백 함수를 통해 비동기적으로 전달한다.
     */
    void try_register_nickname_async(const std::string& nickname, 
                                      SessionPtr session, 
                                      std::function<void(bool)> handler);

    /** 
     * @brief 닉네임 등록 해제.
     * @param nickname 해제할 닉네임.
     * @details 동기화 보호 하에 `nicknames_` 맵에서 해당 닉네임을 제거한다.
     */
    void unregister_nickname(const std::string& nickname);
    
    /**
     * @brief 닉네임으로 활성 세션 찾기 (비동기).
     * @param nickname 찾을 닉네임.
     * @param handler 결과를 받을 콜백 함수. `void(std::shared_ptr<ChatSession> session)` 형태.
     * @details Strand 위에서 세션을 찾고 결과를 콜백으로 전달한다.
     */
    void find_session_by_nickname_async(const std::string& nickname, std::function<void(SessionPtr)> handler);
    
    /**
     * @brief 현재 연결된 모든 사용자(세션)의 닉네임 목록 조회 (비동기).
     * @param handler 결과를 받을 콜백 함수. `void(std::vector<std::string> users)` 형태.
     * @details Strand 위에서 세션 목록을 조회하고 결과를 콜백으로 전달한다.
     */
    void get_user_list_async(std::function<void(std::vector<std::string>)> handler);
    
    /**
     * @brief 관리자 권한으로 사용자 강제 퇴장 (구현 필요).
     * @param admin 관리자 권한을 가진 세션 (`shared_ptr`).
     * @param target_nick 퇴장시킬 사용자 닉네임.
     * @param reason 퇴장 사유 (클라이언트에게 전달될 수 있음).
     * @return 성공 시 true, 실패 시 false (권한 부족, 대상 없음 등).
     */
    bool kick_user(SessionPtr admin, 
                  const std::string& target_nick, 
                  const std::string& reason);
    
    // --- 채팅방 관련 메서드 (간소화 및 비동기) ---
    /**
     * @brief 채팅방 입장 (방이 없으면 생성, 비동기).
     * @param room_name 입장할 채팅방 이름 (유효성 검사 수행).
     * @param session 입장할 세션 (`shared_ptr`).
     * @param handler 작업 완료 시 호출될 콜백 함수. `void(bool success)` 형태.
     * @details Strand 위에서 방 참여 로직을 수행하고 결과를 콜백으로 전달한다.
     */
    void join_room_async(const std::string& room_name, 
                         SessionPtr session, 
                         std::function<void(bool)> handler);
    
    /**
     * @brief 채팅방 입장 (방이 없으면 생성, 동기).
     * @param room_name 입장할 채팅방 이름 (유효성 검사 수행).
     * @param session 입장할 세션 (`shared_ptr`).
     * @return 성공 시 true, 실패 시 false.
     */
    bool join_room(const std::string& room_name, SessionPtr session);
    
    /**
     * @brief 현재 참여 중인 채팅방에서 퇴장 (비동기).
     * @param room_name 퇴장할 채팅방 이름.
     * @param session 퇴장할 세션 (`shared_ptr`).
     * @param handler 작업 완료 시 호출될 콜백 함수. `void(bool success)` 형태.
     * @details Strand 위에서 방 퇴장 로직을 수행하고 결과를 콜백으로 전달한다.
     */
    void leave_room_async(const std::string& room_name, 
                          SessionPtr session, 
                          std::function<void(bool)> handler);
                          
    /**
     * @brief 현재 참여 중인 채팅방에서 퇴장 (동기).
     * @param room_name 퇴장할 채팅방 이름.
     * @param session 퇴장할 세션 (`shared_ptr`).
     * @return 성공 시 true, 실패 시 false.
     */
    bool leave_room(const std::string& room_name, SessionPtr session);

    /**
     * @brief 특정 세션이 참여 중인 모든 채팅방에서 퇴장시킨다.
     * @param session 퇴장시킬 세션 (`shared_ptr`).
     * @details `ChatServer::leave()` 내부에서 호출되어 클라이언트 연결 종료 시 정리 작업을 수행한다.
     *          모든 `rooms_`를 순회하며 해당 세션을 제거하고, 방이 비면 방 자체도 제거한다.
     *          각 방의 남은 참여자에게 퇴장 알림을 전송한다.
     */
    void leave_all_rooms(SessionPtr session);
    
    /**
     * @brief 닉네임으로 활성 세션 찾기 (동기).
     * @param nickname 찾을 닉네임.
     * @return 해당 닉네임을 가진 세션, 없으면 nullptr.
     */
    SessionPtr find_session_by_nickname(const std::string& nickname);
    
    /**
     * @brief 현재 연결된 모든 사용자 목록 (동기).
     * @return 모든 사용자의 닉네임 목록.
     */
    std::vector<std::string> get_user_list();

    /**
     * @brief 특정 채팅방에 메시지 브로드캐스트.
     * @param room_name 메시지를 보낼 채팅방 이름.
     * @param message 전송할 메시지 내용.
     * @param sender 메시지를 보낸 세션 (`shared_ptr`, 해당 세션에는 보내지 않음). `nullptr`이면 모든 멤버에게 전송.
     * @return 방이 존재하고 메시지 전송(시도) 시 true, 방이 없으면 false.
     * @details 동기화 하에 `rooms_` 맵에서 해당 방을 찾아 참여 중인 모든 세션(sender 제외)에게 메시지를 전달한다.
     */
    bool broadcast_to_room(const std::string& room_name, 
                           const std::string& message, 
                           SessionPtr sender);
                         
    // 사용자 인증/등록/수정/삭제 메서드 선언 (구현 필요)
    bool authenticate_user(const std::string& username, const std::string& password, SessionPtr session);
    bool register_user(const std::string& username, const std::string& password, bool is_admin = false);
    bool update_user(const std::string& username, const std::string& new_password, int is_admin, SessionPtr admin_session);
    bool delete_user(const std::string& username, SessionPtr admin_session);
    
    // 파일 전송 관련 메서드 선언 (구현 필요)
    std::string init_file_transfer(const std::string& filename, size_t filesize, SessionPtr sender, const std::string& receiver_nick);
    bool accept_file_transfer(const std::string& transfer_id, SessionPtr session);
    bool reject_file_transfer(const std::string& transfer_id, SessionPtr session);
    bool process_file_data(const std::string& transfer_id, const char* data, size_t length, SessionPtr session);
    bool complete_file_transfer(const std::string& transfer_id, SessionPtr session);
    
    // 메시지 히스토리 관련 메서드 선언 (구현 필요)
    void set_history_enabled(bool enable);
    bool is_history_enabled() const;
    std::vector<std::string> load_global_history(size_t limit = 50);
    std::vector<std::string> load_private_history(const std::string& user1, const std::string& user2, size_t limit = 50);
    std::vector<std::string> load_room_history(const std::string& room, size_t limit = 50);

private:
    /** 
     * @brief 내부적으로 리스너를 생성하고 시작하는 함수.
     * @details run() 메서드 내부에서 호출되어 실제 리스닝을 시작한다.
     *          shared_from_this()를 안전하게 호출하기 위해 분리됨.
     * @return 성공 시 true, 실패 시 false.
     */
    bool start_listening();

    /** 
     * @brief 서버 종료 시그널(SIGINT, SIGTERM) 대기 시작.
     * @details `signals_` 객체를 사용하여 비동기적으로 시그널을 기다리고, 수신 시 `stop()` 메서드를 호출한다.
     */
    void do_await_stop();
    
    /**
     * @brief 비밀번호 해싱 함수 (구현 필요).
     * @param password 평문 비밀번호.
     * @return 해시된 비밀번호 문자열 (예: bcrypt, scrypt 사용 권장).
     */
    std::string hash_password(const std::string& password);
    
    /**
     * @brief 전역 시스템 알림 메시지 전송.
     * @param message 알림 메시지 내용.
     * @details `broadcast`를 사용하여 모든 활성 세션에게 시스템 메시지를 전달한다.
     */
    void system_broadcast(const std::string& message); // 전역 시스템 메시지용으로 유지

    // Strand 내부에서 호출될 헬퍼 함수들
    void broadcast_impl(const std::string& message, const SessionPtr& sender);
    void leave_all_rooms_impl(const SessionPtr& session);
    void try_register_nickname_impl(const std::string& nickname_copy, std::weak_ptr<SessionInterface> weak_session, std::function<void(bool)> handler);
};