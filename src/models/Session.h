#pragma once
#include <ctime>
#include <string>

enum class SessionStatus {
    Scheduled,
    Completed,
    Cancelled
};

inline std::string sessionStatusToString(SessionStatus status) {
    switch(status) {
        case SessionStatus::Scheduled: return "scheduled";
        case SessionStatus::Completed: return "completed";
        case SessionStatus::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

struct Session {
    int id = 0;
    int slotId = 0;
    int clientId = 0;
    int psychologistId = 0;
    SessionStatus status = SessionStatus::Scheduled;
    std::time_t createdAt = 0;
    

    long long startTime = 0;
    int duration = 0;
    std::string clientName;
    std::string psychologistName;
    std::string format;
};
