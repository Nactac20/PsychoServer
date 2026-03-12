#ifdef _WIN32
#include <windows.h>
#endif
#include "src/database/Database.h"
#include "src/models/User.h"
#include "src/models/Slot.h"
#include "src/utils/Logger.h"
#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>
#include <chrono>
#include <atomic>

void printUser(const User& user) {
    std::cout << "ID: " << user.id 
              << ", Name: " << user.name 
              << ", Email: " << user.email
              << ", Role: " << (user.isPsychologist() ? "Psychologist" : "Client");
    if (user.isPsychologist()) {
        std::cout << ", Spec: " << user.specialization;
    }
    std::cout << std::endl;
}

void printSlot(const Slot& slot) {
    time_t t = slot.toTimeT();
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M", std::localtime(&t));
    
    std::cout << "Slot ID: " << slot.id 
              << ", Time: " << buffer
              << ", Duration: " << slot.durationMinutes << " min"
              << ", Format: " << slot.format
              << ", Status: " << (slot.isFree() ? "FREE" : "BOOKED")
              << std::endl;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    
    Logger::instance().setLogFile("psycho_server.log");
 
    LOG_INFO("Starting PsychoServer demo");
    
    Database db(":memory:");
    
    if (!db.initialize()) {
        LOG_ERROR("Failed to initialize database!");
        return 1;
    }
    
    User psychologist;
    psychologist.name = "Александра Петрова";
    psychologist.email = "a.petrova@psy.ru";
    psychologist.passwordHash = hashPassword("psy123");
    psychologist.role = UserRole::Psychologist;
    psychologist.specialization = "Family therapist";
    psychologist.education = "MSU, 2010";
    psychologist.description = "12 years experience";
    
    auto psyId = db.registerUser(psychologist);
    if (!psyId) {
        LOG_ERROR("Failed to register psychologist!");
        return 1;
    }
    
    std::cout << "\nPsychologist registered:\n  ";
    psychologist.id = *psyId;
    printUser(psychologist);
    
    User client;
    client.name = "Елена Смирнова";
    client.email = "elena@mail.ru";
    client.passwordHash = hashPassword("client456");
    client.role = UserRole::Client;
    
    auto clientId = db.registerUser(client);
    if (!clientId) {
        LOG_ERROR("Failed to register client!");
        return 1;
    }
    
    std::cout << "\nClient registered:\n  ";
    client.id = *clientId;
    printUser(client);
    
    auto now = std::chrono::system_clock::now();
    
    Slot slot1;
    slot1.psychologistId = *psyId;
    slot1.startTime = now + std::chrono::hours(24);
    slot1.durationMinutes = 60;
    slot1.format = "online";
    
    Slot slot2;
    slot2.psychologistId = *psyId;
    slot2.startTime = now + std::chrono::hours(48);
    slot2.durationMinutes = 90;
    slot2.format = "offline";
    
    if (db.addSlot(slot1) && db.addSlot(slot2)) {
        LOG_INFO("Added 2 slots for psychologist " + std::to_string(*psyId));
        std::cout << "\nAdded 2 slots for psychologist\n";
    } else {
        LOG_ERROR("Failed to add slots");
        std::cout << "\nFailed to add slots\n";
    }
    
    std::cout << "\nFree slots:\n";
    auto freeSlots = db.getFreeSlots(*psyId);
    for (const auto& slot : freeSlots) {
        std::cout << "  ";
        printSlot(slot);
    }
    
    if (!freeSlots.empty()) {
        std::cout << "\nBooking slot " << freeSlots[0].id << "...\n";
        int sessionId = db.bookSlot(freeSlots[0].id, *clientId);
        
        if (sessionId > 0) {
            std::cout << "Booked! Session ID: " << sessionId << std::endl;
            LOG_INFO("Successfully booked slot " + std::to_string(freeSlots[0].id) + 
                     " for client " + std::to_string(*clientId));
        } else {
            std::cout << "Booking failed!\n";
            LOG_ERROR("Booking failed for slot " + std::to_string(freeSlots[0].id));
        }
    }
    
    std::cout << "\nFree slots after booking:\n";
    freeSlots = db.getFreeSlots(*psyId);
    for (const auto& slot : freeSlots) {
        std::cout << "  ";
        printSlot(slot);
    }
    
    std::cout << "\nTesting booking with non-existent client...\n";
    if (!freeSlots.empty()) {
        int sessionId = db.bookSlot(freeSlots[0].id, 99999);
        if (sessionId == 0) {
            std::cout << "Correctly rejected booking for non-existent client\n";
        }
    }
    
    std::cout << "\nTesting expired slot...\n";
    Slot expiredSlot;
    expiredSlot.psychologistId = *psyId;
    expiredSlot.startTime = now - std::chrono::hours(24);
    expiredSlot.durationMinutes = 60;
    expiredSlot.format = "online";
    
    if (db.addSlot(expiredSlot)) {
        std::cout << "Added expired slot for testing\n";
        auto slots = db.getFreeSlots(*psyId);
        bool found = false;
        for (const auto& s : slots) {
            if (s.toTimeT() < std::time(nullptr)) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "Expired slot not shown in free slots\n";
        }
    }
    
    std::cout << "\nTesting concurrent booking (10 threads)...\n";
    
    Slot testSlot;
    testSlot.psychologistId = *psyId;
    testSlot.startTime = now + std::chrono::hours(72);
    testSlot.durationMinutes = 60;
    testSlot.format = "online";
    
    if (db.addSlot(testSlot)) {
        auto testSlots = db.getFreeSlots(*psyId);
        if (!testSlots.empty()) {
            int testSlotId = testSlots.back().id;
            
            std::vector<int> testClientIds;
            for (int i = 0; i < 10; i++) {
                User testClient;
                testClient.name = "Test Client " + std::to_string(i);
                testClient.email = "test.client" + std::to_string(i) + "@example.com";
                testClient.passwordHash = hashPassword("pass");
                testClient.role = UserRole::Client;
                
                auto id = db.registerUser(testClient);
                if (id.has_value()) {
                    testClientIds.push_back(id.value());
                }
            }
            
            std::vector<std::thread> threads;
            std::atomic<int> successCount{0};
            
            for (int i = 0; i < 10; ++i) {
                threads.emplace_back([&db, testSlotId, i, &testClientIds, &successCount]() {
                    int sessionId = db.bookSlot(testSlotId, testClientIds[i]);
                    if (sessionId > 0) {
                        successCount++;
                    }
                });
            }
            
            for (auto& t : threads) {
                t.join();
            }
            
            std::cout << "  Results: " << successCount 
                      << " successful bookings (should be 1)\n";
            std::cout << "  " << (successCount == 1 ? "PASSED" : "FAILED") << "\n";
            
            if (successCount == 1) {
                LOG_INFO("Concurrent booking test passed");
            } else {
                LOG_ERROR("Concurrent booking test failed: " + std::to_string(successCount) + 
                          " successes");
            }
        }
    }

    LOG_INFO("PsychoServer demo completed successfully");
    
    return 0;
}