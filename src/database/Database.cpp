#include "Database.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>

Database::Database(const std::string& dbPath) : m_dbPath(dbPath) {
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open database: " + std::string(sqlite3_errmsg(m_db)));
        m_db = nullptr;
    } else {
        LOG_INFO("Connected to database: " + dbPath);
    }
}

Database::~Database() {
    if (m_db) {
        sqlite3_close(m_db);
        LOG_INFO("Database closed");
    }
}

bool Database::executeSQL(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQL execute failed: " + std::string(errMsg ? errMsg : "unknown error"));
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

std::string Database::escapeString(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\'') result += "''";
        else result += c;
    }
    return result;
}

bool Database::initialize() {
    if (!m_db) {
        LOG_ERROR("Cannot initialize: database not open");
        return false;
    }
    
    std::vector<std::string> tables = {
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "email TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT CHECK(role IN ('client', 'psychologist')) NOT NULL,"
        "specialization TEXT,"
        "education TEXT,"
        "description TEXT,"
        "photo_path TEXT)",

        "CREATE TABLE IF NOT EXISTS slots ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "psychologist_id INTEGER NOT NULL,"
        "start_time INTEGER NOT NULL,"
        "duration_minutes INTEGER DEFAULT 60,"
        "format TEXT DEFAULT 'online',"
        "is_booked INTEGER DEFAULT 0,"
        "booked_by_client_id INTEGER,"
        "FOREIGN KEY (psychologist_id) REFERENCES users(id))",

        "CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "slot_id INTEGER UNIQUE NOT NULL,"
        "client_id INTEGER NOT NULL,"
        "psychologist_id INTEGER NOT NULL,"
        "status TEXT DEFAULT 'scheduled',"
        "created_at INTEGER DEFAULT (strftime('%s', 'now')),"
        "FOREIGN KEY (slot_id) REFERENCES slots(id),"
        "FOREIGN KEY (client_id) REFERENCES users(id),"
        "FOREIGN KEY (psychologist_id) REFERENCES users(id))",

        "CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);",
        "CREATE INDEX IF NOT EXISTS idx_slots_psychologist ON slots(psychologist_id);",
        "CREATE INDEX IF NOT EXISTS idx_slots_start_time ON slots(start_time);"
    };
    
    for (const auto& sql : tables) {
        if (!executeSQL(sql)) {
            LOG_ERROR("Failed to create table: " + sql.substr(0, 50) + "...");
            return false;
        }
    }
    
    LOG_INFO("Database initialized successfully");
    return true;
}

std::optional<int> Database::registerUser(const User& user) {
    if (!user.isValidEmail()) {
        LOG_ERROR("Invalid email format: " + user.email);
        return std::nullopt;
    }
    
    std::string roleStr = user.role == UserRole::Client ? "client" : "psychologist";
    
    std::string sql;
    if (user.role == UserRole::Psychologist) {
        sql = "INSERT INTO users (name, email, password_hash, role, "
              "specialization, education, description, photo_path) VALUES ('"
              + escapeString(user.name) + "', '"
              + escapeString(user.email) + "', '"
              + escapeString(user.passwordHash) + "', '"
              + roleStr + "', '"
              + escapeString(user.specialization) + "', '"
              + escapeString(user.education) + "', '"
              + escapeString(user.description) + "', '"
              + escapeString(user.photoPath) + "')";
    } else {
        sql = "INSERT INTO users (name, email, password_hash, role) VALUES ('"
              + escapeString(user.name) + "', '"
              + escapeString(user.email) + "', '"
              + escapeString(user.passwordHash) + "', '"
              + roleStr + "')";
    }
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to register user: " + std::string(errMsg ? errMsg : "unknown error"));
        if (errMsg) sqlite3_free(errMsg);
        return std::nullopt;
    }
    
    int newId = sqlite3_last_insert_rowid(m_db);
    LOG_INFO("User registered with ID: " + std::to_string(newId) + ", email: " + user.email);
    return newId;
}

std::optional<User> Database::findUserByEmail(const std::string& email) {
    std::string sql = "SELECT id, name, email, password_hash, role, "
                      "specialization, education, description, photo_path "
                      "FROM users WHERE email = '" + escapeString(email) + "'";
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare find user query: " + std::string(sqlite3_errmsg(m_db)));
        return std::nullopt;
    }
    
    std::optional<User> result;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User user;
        user.id = sqlite3_column_int(stmt, 0);
        user.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        user.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        user.passwordHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        
        std::string roleStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        user.role = userRoleFromString(roleStr);
        
        if (user.role == UserRole::Psychologist) {
            if (sqlite3_column_text(stmt, 5)) 
                user.specialization = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (sqlite3_column_text(stmt, 6)) 
                user.education = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            if (sqlite3_column_text(stmt, 7)) 
                user.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            if (sqlite3_column_text(stmt, 8)) 
                user.photoPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        }
        
        result = user;
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::optional<User> Database::findUserById(int id) {
    std::string sql = "SELECT id, name, email, password_hash, role, "
                      "specialization, education, description, photo_path "
                      "FROM users WHERE id = " + std::to_string(id);
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare find user by id query: " + std::string(sqlite3_errmsg(m_db)));
        return std::nullopt;
    }
    
    std::optional<User> result;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User user;
        user.id = sqlite3_column_int(stmt, 0);
        user.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        user.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        user.passwordHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        
        std::string roleStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        user.role = userRoleFromString(roleStr);
        
        if (user.role == UserRole::Psychologist) {
            if (sqlite3_column_text(stmt, 5)) 
                user.specialization = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (sqlite3_column_text(stmt, 6)) 
                user.education = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            if (sqlite3_column_text(stmt, 7)) 
                user.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            if (sqlite3_column_text(stmt, 8)) 
                user.photoPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        }
        
        result = user;
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<User> Database::getAllPsychologists() {
    std::string sql = "SELECT id, name, email, password_hash, role, "
                      "specialization, education, description, photo_path "
                      "FROM users WHERE role = 'psychologist'";
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    std::vector<User> result;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare get psychologists query: " + 
                  std::string(sqlite3_errmsg(m_db)));
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        User user;
        user.id = sqlite3_column_int(stmt, 0);
        user.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        user.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        user.passwordHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        user.role = UserRole::Psychologist;
        
        if (sqlite3_column_text(stmt, 5)) 
            user.specialization = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (sqlite3_column_text(stmt, 6)) 
            user.education = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (sqlite3_column_text(stmt, 7)) 
            user.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_text(stmt, 8)) 
            user.photoPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        
        result.push_back(user);
    }
    
    sqlite3_finalize(stmt);
    LOG_INFO("Found " + std::to_string(result.size()) + " psychologists");
    return result;
}

bool Database::validatePassword(const std::string& email, const std::string& password) {
    auto user = findUserByEmail(email);
    if (!user) {
        LOG_WARNING("Password validation failed: user not found - " + email);
        return false;
    }
    bool valid = (user->passwordHash == hashPassword(password));
    if (!valid) {
        LOG_WARNING("Password validation failed: incorrect password for " + email);
    }
    return valid;
}

bool Database::addSlot(const Slot& slot) {
    auto psychologist = findUserById(slot.psychologistId);
    if (!psychologist.has_value() || !psychologist->isPsychologist()) {
        LOG_ERROR("Cannot add slot: psychologist " + std::to_string(slot.psychologistId) + 
                  " does not exist");
        return false;
    }
    
    std::string sql = "INSERT INTO slots (psychologist_id, start_time, duration_minutes, format) VALUES ("
        + std::to_string(slot.psychologistId) + ", "
        + std::to_string(slot.toTimeT()) + ", "
        + std::to_string(slot.durationMinutes) + ", '"
        + escapeString(slot.format) + "')";
    
    bool success = executeSQL(sql);
    if (success) {
        LOG_INFO("Slot added for psychologist " + std::to_string(slot.psychologistId));
    } else {
        LOG_ERROR("Failed to add slot for psychologist " + std::to_string(slot.psychologistId));
    }
    return success;
}

std::vector<Slot> Database::getFreeSlots(int psychologistId) {
    auto psychologist = findUserById(psychologistId);
    if (!psychologist.has_value() || !psychologist->isPsychologist()) {
        LOG_WARNING("getFreeSlots called for non-existent psychologist: " + 
                    std::to_string(psychologistId));
        return {};
    }
    
    auto now = std::chrono::system_clock::now();
    time_t now_t = std::chrono::system_clock::to_time_t(now);
    
    std::string sql = "SELECT id, psychologist_id, start_time, duration_minutes, "
                      "format, is_booked, booked_by_client_id "
                      "FROM slots "
                      "WHERE psychologist_id = " + std::to_string(psychologistId) + " "
                      "AND is_booked = 0 "
                      "AND start_time > " + std::to_string(now_t) + " "
                      "ORDER BY start_time";
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    std::vector<Slot> result;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare get free slots query: " + std::string(sqlite3_errmsg(m_db)));
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Slot slot;
        slot.id = sqlite3_column_int(stmt, 0);
        slot.psychologistId = sqlite3_column_int(stmt, 1);
        time_t startTime = sqlite3_column_int64(stmt, 2);
        slot.startTime = std::chrono::system_clock::from_time_t(startTime);
        slot.durationMinutes = sqlite3_column_int(stmt, 3);
        slot.format = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        slot.isBooked = sqlite3_column_int(stmt, 5) != 0;
        slot.bookedByClientId = sqlite3_column_int(stmt, 6);
        
        result.push_back(slot);
    }
    
    sqlite3_finalize(stmt);
    LOG_INFO("Found " + std::to_string(result.size()) + " free slots for psychologist " + 
             std::to_string(psychologistId));
    return result;
}

int Database::bookSlot(int slotId, int clientId) {
    auto client = findUserById(clientId);
    if (!client.has_value()) {
        LOG_ERROR("Booking failed: client " + std::to_string(clientId) + " does not exist");
        return 0;
    }
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_ERROR("Failed to begin transaction: " + std::string(errMsg ? errMsg : "unknown error"));
        if (errMsg) sqlite3_free(errMsg);
        return 0;
    }
    
    std::string checkSql = "SELECT is_booked, start_time, psychologist_id FROM slots "
                           "WHERE id = " + std::to_string(slotId);
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, checkSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare check slot query: " + std::string(sqlite3_errmsg(m_db)));
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return 0;
    }
    
    bool isFree = false;
    int psychologistId = 0;
    time_t startTime = 0;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        isFree = (sqlite3_column_int(stmt, 0) == 0);
        startTime = sqlite3_column_int64(stmt, 1);
        psychologistId = sqlite3_column_int(stmt, 2);
    }
    sqlite3_finalize(stmt);
    
    auto psychologist = findUserById(psychologistId);
    if (!psychologist.has_value()) {
        LOG_ERROR("Booking failed: psychologist " + std::to_string(psychologistId) + 
                  " does not exist");
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return 0;
    }
    
    if (startTime < std::time(nullptr)) {
        LOG_ERROR("Booking failed: slot " + std::to_string(slotId) + " is expired");
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return 0;
    }
    
    if (!isFree) {
        LOG_WARNING("Booking failed: slot " + std::to_string(slotId) + " is already booked");
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return 0;
    }
    
    std::string updateSql = "UPDATE slots SET is_booked = 1, booked_by_client_id = " +
                            std::to_string(clientId) +
                            " WHERE id = " + std::to_string(slotId) +
                            " AND is_booked = 0";
    
    int rc = sqlite3_exec(m_db, updateSql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK || sqlite3_changes(m_db) == 0) {
        LOG_ERROR("Failed to update slot: " + std::string(errMsg ? errMsg : "no rows updated"));
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        if (errMsg) sqlite3_free(errMsg);
        return 0;
    }
    
    std::string insertSessionSql = 
        "INSERT INTO sessions (slot_id, client_id, psychologist_id) VALUES (" +
        std::to_string(slotId) + ", " +
        std::to_string(clientId) + ", " +
        std::to_string(psychologistId) + ")";
    
    rc = sqlite3_exec(m_db, insertSessionSql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create session: " + std::string(errMsg ? errMsg : "unknown error"));
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        if (errMsg) sqlite3_free(errMsg);
        return 0;
    }
    
    int sessionId = sqlite3_last_insert_rowid(m_db);
    
    sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr);
    
    LOG_INFO("Slot " + std::to_string(slotId) + " booked by client " + 
             std::to_string(clientId) + ", session ID: " + std::to_string(sessionId));
    
    return sessionId;
}