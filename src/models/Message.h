#pragma once
#include <string>
#include <ctime>

struct Message {
    int id = 0;
    int sessionId = 0;
    int senderId = 0;
    std::string senderName;
    std::string text;
    std::time_t timestamp = 0;
};