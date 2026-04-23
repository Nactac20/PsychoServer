#pragma once
#include <boost/asio.hpp>
#include <memory>
#include "RequestHandler.h"

class NetworkSession : public std::enable_shared_from_this<NetworkSession> {
public:
    NetworkSession(boost::asio::ip::tcp::socket socket, 
                   std::shared_ptr<RequestHandler> handler);
    
    void start();

private:
    void doRead();
    void doWrite(const std::string& response);
    
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::streambuf m_buffer;
    std::shared_ptr<RequestHandler> m_handler;
};
