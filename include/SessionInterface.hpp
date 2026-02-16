#ifndef SESSION_INTERFACE_HPP
#define SESSION_INTERFACE_HPP

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#include <string>
#include <memory>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

namespace net = boost::asio;

/// 세션의 공통 인터페이스
class SessionInterface
{
public:
    virtual ~SessionInterface() = default;
    
    /// 클라이언트에게 메시지 전달
    virtual void deliver(const std::string& msg) = 0;
    
    /// 세션 종료
    virtual void stop_session() = 0;
    
    /// 닉네임 getter
    virtual const std::string& nickname() const = 0;
    
    /// 원격 ID (IP:port) getter
    virtual const std::string& remote_id() const = 0;
    
    /// Strand getter
    virtual net::strand<net::any_io_executor>& get_strand() = 0;
    
    /// 인증 상태 getter
    virtual bool is_authenticated() const = 0;
    
    /// 닉네임 setter
    virtual void set_nickname(const std::string& nick) = 0;
    
    /// 인증 상태 setter
    virtual void set_authenticated(bool auth) = 0;

    /// 현재 방 getter
    virtual const std::string& current_room() const = 0;

    /// 현재 방 setter
    virtual void set_current_room(const std::string& room_name) = 0;

    // shared_ptr을 인터페이스 타입으로 반환하기 위한 헬퍼
    virtual std::shared_ptr<SessionInterface> shared_from_this() = 0;
};

/// @brief SessionInterface에 대한 공유 포인터 타입 정의.
using SessionPtr = std::shared_ptr<SessionInterface>;

#endif // SESSION_INTERFACE_HPP 