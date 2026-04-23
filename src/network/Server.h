#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <vector>
#include "RequestHandler.h"

class Server {
public:
    Server(short port, std::shared_ptr<Database> db, int threadCount = 0);
    ~Server();
    
    void run();
    void stop();

private:
    void startAccept();
    
    boost::asio::io_context m_ioContext;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::shared_ptr<RequestHandler> m_handler;
    std::vector<std::thread> m_threadPool;
    bool m_running = false;
};
