#pragma once
#include <string>
#include <chrono>

struct Slot {
    int id = 0;
    int psychologistId = 0;
    std::chrono::system_clock::time_point startTime;
    int durationMinutes = 60;
    std::string format;
    bool isBooked = false;
    int bookedByClientId = 0;
    
    bool isExpired() const {
        auto now = std::chrono::system_clock::now();
        auto endTime = startTime + std::chrono::minutes(durationMinutes);
        return endTime < now;
    }
    
    bool isFree() const {
        return !isBooked && !isExpired();
    }
    
    std::time_t toTimeT() const {
        return std::chrono::system_clock::to_time_t(startTime);
    }
    
    static Slot fromTimeT(std::time_t t, int psychologistId = 0) {
        Slot slot;
        slot.psychologistId = psychologistId;
        slot.startTime = std::chrono::system_clock::from_time_t(t);
        return slot;
    }
};