#include "ChatListener.hpp"
#include "ChatServer.hpp"   // Need full definition for server_
#include "ChatSession.hpp" // Need full definition to create ChatSession
#include "spdlog/spdlog.h"
#include <boost/asio/strand.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <system_error> // For std::system_error

namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;

//------------------------------------------------------------------------------
// ChatListener Implementation
//------------------------------------------------------------------------------
ChatListener::ChatListener(
    net::io_context &ioc,
    tcp::endpoint endpoint,
    std::shared_ptr<ChatServer> server)
    : ioc_(ioc),
      acceptor_(net::make_strand(ioc)),
      server_(server)
{
    beast::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
    {
        spdlog::error("Listener open error: {}", ec.message());
        throw std::system_error{ec};
    }

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
    {
        spdlog::error("Listener set_option error: {}", ec.message());
        acceptor_.close();
        throw std::system_error{ec};
    }

    acceptor_.bind(endpoint, ec);
    if (ec)
    {
        spdlog::error("Listener bind error: {}", ec.message());
        acceptor_.close();
        throw std::system_error{ec};
    }

    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        spdlog::error("Listener listen error: {}", ec.message());
        acceptor_.close();
        throw std::system_error{ec};
    }

    spdlog::info("[ChatListener] Listening on {}:{}", endpoint.address().to_string(), endpoint.port());
}

void ChatListener::run()
{
    if (!acceptor_.is_open())
    {
        spdlog::error("[ChatListener] Acceptor not open. Cannot run.");
        return;
    }
    spdlog::info("[ChatListener] Starting accept loop...");
    do_accept();
}

void ChatListener::do_accept()
{
    if (!acceptor_.is_open())
        return;

    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&ChatListener::on_accept, shared_from_this()));
}

void ChatListener::on_accept(beast::error_code ec, tcp::socket socket)
{
    if (!acceptor_.is_open())
    {
        spdlog::info("[ChatListener] on_accept called but acceptor is closed.");
        return;
    }

    if (!ec)
    {
        try
        {
            spdlog::info("[ChatListener] Accepted connection from {}:{}",
                         socket.remote_endpoint().address().to_string(),
                         socket.remote_endpoint().port());
            std::make_shared<ChatSession>(std::move(socket), server_)->start();
        }
        catch (const std::exception &e)
        {
            spdlog::error("[ChatListener] Exception during session creation/start: {}", e.what());
        }
    }
    else
    {
        if (ec != net::error::operation_aborted)
        {
            spdlog::error("[ChatListener] Accept error: {}", ec.message());
        }
    }

    if (acceptor_.is_open())
    {
        do_accept();
    }
}