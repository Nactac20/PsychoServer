#include "Server.h"
#include "NetworkSession.h"
#include "../utils/Logger.h"


Server::Server(short port, std::shared_ptr<Database> db, int threadCount)
    : m_acceptor(m_ioContext, 
                 boost::asio::ip::tcp::endpoint(
                     boost::asio::ip::tcp::v4(), port))
    , m_handler(std::make_shared<RequestHandler>(db))
    , m_running(true) {
    
    LOG_INFO("Server starting on port " + std::to_string(port));
    
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
    }
    
    startAccept();
    
    for (int i = 0; i < threadCount; ++i) {
        m_threadPool.emplace_back([this]() {
            m_ioContext.run();
        });
    }
    
    LOG_INFO("Started " + std::to_string(threadCount) + " threads");
}

Server::~Server() {
    stop();
}

void Server::run() {
    for (auto& thread : m_threadPool) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void Server::stop() {
    m_running = false;
    m_ioContext.stop();
}

void Server::startAccept() {
    if (!m_running) return;
    
    m_acceptor.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec) {
                std::make_shared<NetworkSession>(std::move(socket), m_handler)->start();
            } else {
                LOG_ERROR("Accept error: " + ec.message());
            }
            startAccept();
        });
}