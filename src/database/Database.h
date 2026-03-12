#pragma once
#include "../models/User.h"
#include "../models/Slot.h"
#include "../models/Session.h"
#include "../utils/Logger.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>

class Database {
public:
    explicit Database(const std::string& dbPath);
    ~Database();
    
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    
    bool initialize();
    
    std::optional<int> registerUser(const User& user);
    std::optional<User> findUserByEmail(const std::string& email);
    std::optional<User> findUserById(int id);
    bool validatePassword(const std::string& email, const std::string& password);
    
    std::vector<User> getAllPsychologists();
    
    bool addSlot(const Slot& slot);
    std::vector<Slot> getFreeSlots(int psychologistId);
    
    int bookSlot(int slotId, int clientId);
    
private:
    bool executeSQL(const std::string& sql);
    std::string escapeString(const std::string& str);
    
    sqlite3* m_db = nullptr;
    std::string m_dbPath;
    std::recursive_mutex m_mutex;
};