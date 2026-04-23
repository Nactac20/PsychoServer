#include "NetworkSession.h"
#include "../utils/Logger.h"
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

NetworkSession::NetworkSession(boost::asio::ip::tcp::socket socket, std::shared_ptr<RequestHandler> handler): m_socket(std::move(socket)), m_handler(handler) {
    LOG_INFO("NetworkSession created");
}

void NetworkSession::start() {
    LOG_INFO("New client connected");
    doRead();
}

void NetworkSession::doRead() {
    auto self = shared_from_this();
    boost::asio::async_read_until(m_socket, m_buffer, '\n',
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                std::string data{
                    boost::asio::buffers_begin(m_buffer.data()),
                    boost::asio::buffers_begin(m_buffer.data()) + length
                };
                m_buffer.consume(length);
                
                if (!data.empty() && data.back() == '\n')
                    data.pop_back();
                
                LOG_INFO("Received: " + data);
                
                std::string response = m_handler->handleRequest(data);
                doWrite(response);
                doRead();
            } else if (ec != boost::asio::error::eof) {
                LOG_ERROR("Session error: " + ec.message());
            }
        });
}

void NetworkSession::doWrite(const std::string& response) {
    auto self = shared_from_this();
    
    boost::asio::async_write(m_socket, boost::asio::buffer(response),
        [this, self, response](boost::system::error_code ec, std::size_t) {
            if (ec) {
                LOG_ERROR("Write error: " + ec.message());
            } else {
                LOG_INFO("Sent: " + response);
            }
        });
}
