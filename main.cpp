#ifdef _WIN32
#include <windows.h>
#endif
#include "src/database/Database.h"
#include "src/network/Server.h"
#include "src/utils/Logger.h"
#include <csignal>
#include <memory>

std::unique_ptr<Server> g_server;

void signalHandler(int signum) {
    LOG_INFO("Signal " + std::to_string(signum) + " received, stopping server...");
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    
    Logger::instance().setLogFile("psycho_server.log");
    LOG_INFO("Starting PsychoServer network edition");
    
    try {
        unsigned short port = 12345;
        if (argc > 1) {
            port = static_cast<unsigned short>(std::stoi(argv[1]));
        }
        
        auto db = std::make_shared<Database>("psycho.db");
        if (!db->initialize()) {
            LOG_ERROR("Failed to initialize database");
            return 1;
        }
        
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        g_server = std::make_unique<Server>(port, db, 4);
        g_server->run();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    LOG_INFO("Server stopped");
    return 0;
}