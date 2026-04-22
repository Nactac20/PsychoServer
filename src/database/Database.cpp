#include "Database.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>

Database::Database(const std::string& dbPath) : m_dbPath(dbPath), m_running(false) {
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open database: " + std::string(sqlite3_errmsg(m_db)));
        m_db = nullptr;
    } else {
        LOG_INFO("Connected to database: " + dbPath);
    }
}

Database::~Database() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    
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
        "slot_id INTEGER NOT NULL,"
        "client_id INTEGER NOT NULL,"
        "psychologist_id INTEGER NOT NULL,"
        "status TEXT DEFAULT 'scheduled',"
        "created_at INTEGER DEFAULT (strftime('%s', 'now')),"
        "FOREIGN KEY (slot_id) REFERENCES slots(id),"
        "FOREIGN KEY (client_id) REFERENCES users(id),"
        "FOREIGN KEY (psychologist_id) REFERENCES users(id))",

        "CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);",
        "CREATE TABLE IF NOT EXISTS conversations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user1_id INTEGER NOT NULL,"
        "user2_id INTEGER NOT NULL,"
        "UNIQUE(user1_id, user2_id))",

        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "conversation_id INTEGER NOT NULL,"
        "sender_id INTEGER NOT NULL,"
        "text TEXT NOT NULL,"
        "timestamp INTEGER DEFAULT (strftime('%s', 'now')),"
        "FOREIGN KEY (conversation_id) REFERENCES conversations(id),"
        "FOREIGN KEY (sender_id) REFERENCES users(id))",

        "CREATE TABLE IF NOT EXISTS notifications ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER NOT NULL,"
        "text TEXT NOT NULL,"
        "type TEXT DEFAULT 'info',"
        "is_read INTEGER DEFAULT 0,"
        "created_at INTEGER DEFAULT (strftime('%s', 'now')),"
        "FOREIGN KEY (user_id) REFERENCES users(id))",

        "CREATE INDEX IF NOT EXISTS idx_slots_psychologist ON slots(psychologist_id);",
        "CREATE INDEX IF NOT EXISTS idx_slots_start_time ON slots(start_time);",
        "CREATE INDEX IF NOT EXISTS idx_messages_conversation ON messages(conversation_id);",
        "CREATE INDEX IF NOT EXISTS idx_sessions_client ON sessions(client_id);",
        "CREATE INDEX IF NOT EXISTS idx_sessions_psychologist ON sessions(psychologist_id);",
        "CREATE INDEX IF NOT EXISTS idx_notifications_user ON notifications(user_id);"
    };
    
    for (const auto& sql : tables) {
        if (!executeSQL(sql)) {
            if (sql.find("idx_messages_conversation") != std::string::npos) {
                LOG_INFO("Migration: adding conversation_id to messages table...");
                executeSQL("ALTER TABLE messages ADD COLUMN conversation_id INTEGER NOT NULL DEFAULT 0");
                if (executeSQL(sql)) continue;
            }
            
            LOG_ERROR("Failed to create table: " + sql.substr(0, 50) + "...");
            return false;
        }
    }


    sqlite3_stmt* checkStmt;
    std::string tableSql;
    bool needsMigration = false;
    
    if (sqlite3_prepare_v2(m_db, "SELECT sql FROM sqlite_master WHERE type='table' AND name='sessions'", -1, &checkStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            tableSql = reinterpret_cast<const char*>(sqlite3_column_text(checkStmt, 0));
            if (tableSql.find("slot_id INTEGER UNIQUE") != std::string::npos) {
                needsMigration = true;
            }
        }
        sqlite3_finalize(checkStmt);
    }

    if (needsMigration) {
        LOG_INFO("Migration: removing UNIQUE constraint from sessions.slot_id...");
        executeSQL("DROP TABLE IF EXISTS sessions_new");
        executeSQL("BEGIN TRANSACTION");
        
        bool ok = executeSQL("CREATE TABLE sessions_new ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "slot_id INTEGER NOT NULL,"
                             "client_id INTEGER NOT NULL,"
                             "psychologist_id INTEGER NOT NULL,"
                             "status TEXT DEFAULT 'scheduled',"
                             "created_at INTEGER DEFAULT (strftime('%s', 'now')),"
                             "FOREIGN KEY (slot_id) REFERENCES slots(id),"
                             "FOREIGN KEY (client_id) REFERENCES users(id),"
                             "FOREIGN KEY (psychologist_id) REFERENCES users(id))");
        
        if (ok) ok = executeSQL("INSERT INTO sessions_new (id, slot_id, client_id, psychologist_id, status, created_at) "
                                "SELECT id, slot_id, client_id, psychologist_id, status, created_at FROM sessions");
        
        if (ok) ok = executeSQL("DROP TABLE sessions");
        if (ok) ok = executeSQL("ALTER TABLE sessions_new RENAME TO sessions");
        
        if (ok) {
            executeSQL("CREATE INDEX IF NOT EXISTS idx_sessions_client ON sessions(client_id)");
            executeSQL("CREATE INDEX IF NOT EXISTS idx_sessions_psychologist ON sessions(psychologist_id)");
            executeSQL("COMMIT");
            LOG_INFO("Migration completed successfully.");
        } else {
            executeSQL("ROLLBACK");
            executeSQL("DROP TABLE IF EXISTS sessions_new");
            LOG_ERROR("Migration failed, rolled back changes.");
        }
    }
    
    LOG_INFO("Database initialized successfully");
    
    m_running = true;
    m_workerThread = std::thread(&Database::autoUpdateSessionsLoop, this);
    
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

bool Database::updateUser(const User& user) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string sql = "UPDATE users SET "
                      "name = '" + escapeString(user.name) + "', "
                      "email = '" + escapeString(user.email) + "', ";
    
    if (!user.passwordHash.empty()) {
        sql += "password_hash = '" + escapeString(user.passwordHash) + "', ";
    }
    
    sql += "specialization = '" + escapeString(user.specialization) + "', "
           "education = '" + escapeString(user.education) + "', "
           "description = '" + escapeString(user.description) + "' "
           "WHERE id = " + std::to_string(user.id);
    
    if (executeSQL(sql)) {
        LOG_INFO("User profile updated for ID: " + std::to_string(user.id));
        return true;
    }
    return false;
}

bool Database::deleteUser(int userId) {
    auto user = findUserById(userId);
    if (!user.has_value()) return false;
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    executeSQL("BEGIN TRANSACTION");
    
    try {

        if (user->role == UserRole::Psychologist) {
            executeSQL("UPDATE sessions SET status = 'cancelled' WHERE psychologist_id = " + std::to_string(userId));
            executeSQL("DELETE FROM slots WHERE psychologist_id = " + std::to_string(userId));
        } else {

            std::string getSlotsSql = "SELECT slot_id FROM sessions WHERE client_id = " + std::to_string(userId) + " AND status = 'scheduled'";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(m_db, getSlotsSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    int slotId = sqlite3_column_int(stmt, 0);
                    executeSQL("UPDATE slots SET is_booked = 0, booked_by_client_id = NULL WHERE id = " + std::to_string(slotId));
                }
                sqlite3_finalize(stmt);
            }
            executeSQL("UPDATE sessions SET status = 'cancelled' WHERE client_id = " + std::to_string(userId));
        }
        

        executeSQL("DELETE FROM messages WHERE sender_id = " + std::to_string(userId));
        executeSQL("DELETE FROM notifications WHERE user_id = " + std::to_string(userId));
        executeSQL("DELETE FROM conversations WHERE user1_id = " + std::to_string(userId) + " OR user2_id = " + std::to_string(userId));
        

        std::string deleteUserSql = "DELETE FROM users WHERE id = " + std::to_string(userId);
        if (executeSQL(deleteUserSql)) {
            executeSQL("COMMIT");
            LOG_INFO("User " + std::to_string(userId) + " and all related data deleted successfully.");
            return true;
        } else {
            throw std::runtime_error("Failed to delete user record");
        }
    } catch (const std::exception& e) {
        executeSQL("ROLLBACK");
        LOG_ERROR("Error deleting user " + std::to_string(userId) + ": " + std::string(e.what()));
        return false;
    }
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

bool Database::editSlot(int slotId, long long startTime, int duration, const std::string& format) {
    std::string sql = "UPDATE slots SET start_time = " + std::to_string(startTime) + 
                      ", duration_minutes = " + std::to_string(duration) + 
                      ", format = '" + escapeString(format) + "' " +
                      "WHERE id = " + std::to_string(slotId) + " AND is_booked = 0";
    
    bool success = executeSQL(sql);
    if (success) {
        LOG_INFO("Free slot " + std::to_string(slotId) + " updated.");
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
    

    std::string cleanupSql = "DELETE FROM sessions WHERE slot_id = " + std::to_string(slotId) + " AND status = 'cancelled'";
    executeSQL(cleanupSql);
    
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
    
    getOrCreateConversation(clientId, psychologistId);
    

    addNotification(psychologistId, "К вам записался новый клиент: " + client->name, "session");
    addNotification(clientId, "Вы успешно забронировали сессию у специалиста: " + psychologist->name, "session");
    
    return sessionId;
}

bool Database::deleteSlot(int slotId, int psychologistId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    std::string checkSql = "SELECT is_booked FROM slots WHERE id = " + std::to_string(slotId) +
                           " AND psychologist_id = " + std::to_string(psychologistId);
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, checkSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare delete slot check: " + std::string(sqlite3_errmsg(m_db)));
        return false;
    }
    
    bool canDelete = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        canDelete = (sqlite3_column_int(stmt, 0) == 0);
    }
    sqlite3_finalize(stmt);
    
    if (!canDelete) {
        LOG_WARNING("Cannot delete slot " + std::to_string(slotId) + ": not found, not owned, or already booked");
        return false;
    }
    
    std::string deleteSql = "DELETE FROM slots WHERE id = " + std::to_string(slotId) +
                            " AND psychologist_id = " + std::to_string(psychologistId) +
                            " AND is_booked = 0";
    
    bool success = executeSQL(deleteSql);
    if (success) {
        LOG_INFO("Slot " + std::to_string(slotId) + " deleted by psychologist " + std::to_string(psychologistId));
    }
    return success;
}

std::vector<Session> Database::getSessionsByClientId(int clientId) {
    std::string sql = "SELECT s.id, s.slot_id, s.client_id, s.psychologist_id, s.status, s.created_at, "
                      "sl.start_time, sl.duration_minutes, u.name as psych_name, sl.format "
                      "FROM sessions s "
                      "JOIN slots sl ON s.slot_id = sl.id "
                      "JOIN users u ON s.psychologist_id = u.id "
                      "WHERE s.client_id = " + std::to_string(clientId) + " "
                      "ORDER BY sl.start_time ASC";
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    std::vector<Session> result;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Session s;
            s.id = sqlite3_column_int(stmt, 0);
            s.slotId = sqlite3_column_int(stmt, 1);
            s.clientId = sqlite3_column_int(stmt, 2);
            s.psychologistId = sqlite3_column_int(stmt, 3);
            
            const char* statusText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            std::string statusStr = statusText ? statusText : "scheduled";
            if (statusStr == "completed") s.status = SessionStatus::Completed;
            else if (statusStr == "cancelled") s.status = SessionStatus::Cancelled;
            else s.status = SessionStatus::Scheduled;

            s.createdAt = sqlite3_column_int64(stmt, 5);
            s.startTime = sqlite3_column_int64(stmt, 6);
            s.duration = sqlite3_column_int(stmt, 7);
            s.psychologistName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            s.format = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            result.push_back(s);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::vector<Session> Database::getSessionsByPsychologistId(int psychologistId) {
    std::string sql = "SELECT s.id, s.slot_id, s.client_id, s.psychologist_id, s.status, s.created_at, "
                      "sl.start_time, sl.duration_minutes, u.name as client_name, sl.format "
                      "FROM sessions s "
                      "JOIN slots sl ON s.slot_id = sl.id "
                      "JOIN users u ON s.client_id = u.id "
                      "WHERE s.psychologist_id = " + std::to_string(psychologistId) + " "
                      "ORDER BY sl.start_time ASC";
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    std::vector<Session> result;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Session s;
            s.id = sqlite3_column_int(stmt, 0);
            s.slotId = sqlite3_column_int(stmt, 1);
            s.clientId = sqlite3_column_int(stmt, 2);
            s.psychologistId = sqlite3_column_int(stmt, 3);
            
            const char* statusText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            std::string statusStr = statusText ? statusText : "scheduled";
            if (statusStr == "completed") s.status = SessionStatus::Completed;
            else if (statusStr == "cancelled") s.status = SessionStatus::Cancelled;
            else s.status = SessionStatus::Scheduled;

            s.createdAt = sqlite3_column_int64(stmt, 5);
            s.startTime = sqlite3_column_int64(stmt, 6);
            s.duration = sqlite3_column_int(stmt, 7);
            s.clientName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            s.format = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            result.push_back(s);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::optional<Session> Database::getSessionById(int sessionId) {
    std::string sql = "SELECT s.id, s.slot_id, s.client_id, s.psychologist_id, s.status, s.created_at "
                      "FROM sessions s WHERE s.id = " + std::to_string(sessionId);
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare get session by id: " + std::string(sqlite3_errmsg(m_db)));
        return std::nullopt;
    }
    
    std::optional<Session> result;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Session session;
        session.id = sqlite3_column_int(stmt, 0);
        session.slotId = sqlite3_column_int(stmt, 1);
        session.clientId = sqlite3_column_int(stmt, 2);
        session.psychologistId = sqlite3_column_int(stmt, 3);
        
        std::string statusStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (statusStr == "completed") session.status = SessionStatus::Completed;
        else if (statusStr == "cancelled") session.status = SessionStatus::Cancelled;
        else session.status = SessionStatus::Scheduled;
        
        session.createdAt = sqlite3_column_int64(stmt, 5);
        result = session;
    }
    
    sqlite3_finalize(stmt);
    return result;
}

bool Database::cancelSession(int sessionId) {
    auto session = getSessionById(sessionId);
    if (!session) return false;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    executeSQL("BEGIN TRANSACTION");
    
    std::string sql1 = "UPDATE sessions SET status = 'cancelled' WHERE id = " + std::to_string(sessionId);
    std::string sql2 = "UPDATE slots SET is_booked = 0, booked_by_client_id = NULL WHERE id = " + std::to_string(session->slotId);
    
    bool ok1 = executeSQL(sql1);
    bool ok2 = executeSQL(sql2);
    
    if (ok1 && ok2) {
        executeSQL("COMMIT");
        LOG_INFO("Session " + std::to_string(sessionId) + " cancelled and slot freed.");
        

        auto client = findUserById(session->clientId);
        auto psychologist = findUserById(session->psychologistId);
        if (client && psychologist) {
            addNotification(session->clientId, "Сессия со специалистом " + psychologist->name + " была отменена.", "warning");
            addNotification(session->psychologistId, "Сессия с клиентом " + client->name + " была отменена.", "warning");
        }
        
        return true;
    } else {
        executeSQL("ROLLBACK");
        LOG_ERROR("Failed to cancel session " + std::to_string(sessionId));
        return false;
    }
}

bool Database::updateSessionSlot(int sessionId, long long startTime, int duration, const std::string& format) {
    auto session = getSessionById(sessionId);
    if (!session) return false;
    
    std::string sql = "UPDATE slots SET start_time = " + std::to_string(startTime) + 
                      ", duration_minutes = " + std::to_string(duration) + 
                      ", format = '" + escapeString(format) + "' " +
                      "WHERE id = " + std::to_string(session->slotId);
                      
    bool success = executeSQL(sql);
    if (success) {
        LOG_INFO("Slot updated for session " + std::to_string(sessionId));
    }
    return success;
}

int Database::getOrCreateConversation(int user1Id, int user2Id) {
    if (user1Id > user2Id) std::swap(user1Id, user2Id);
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string selectSql = "SELECT id FROM conversations WHERE user1_id = " + std::to_string(user1Id) + 
                            " AND user2_id = " + std::to_string(user2Id);
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, selectSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            return id;
        }
        sqlite3_finalize(stmt);
    }
    
    std::string insertSql = "INSERT INTO conversations (user1_id, user2_id) VALUES (" + 
                            std::to_string(user1Id) + ", " + std::to_string(user2Id) + ")";
    if (executeSQL(insertSql)) {
        return static_cast<int>(sqlite3_last_insert_rowid(m_db));
    }
    return -1;
}

std::vector<Conversation> Database::getConversations(int userId) {
    std::string sql = "SELECT c.id, "
                      "CASE WHEN c.user1_id = " + std::to_string(userId) + " THEN c.user2_id ELSE c.user1_id END as other_id, "
                      "u.name as other_name "
                      "FROM conversations c "
                      "JOIN users u ON u.id = other_id "
                      "WHERE c.user1_id = " + std::to_string(userId) + " OR c.user2_id = " + std::to_string(userId);
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    std::vector<Conversation> result;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Conversation conv;
        conv.id = sqlite3_column_int(stmt, 0);
        conv.otherUserId = sqlite3_column_int(stmt, 1);
        conv.otherUserName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        result.push_back(conv);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::sendMessage(int conversationId, int senderId, const std::string& text) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    std::string checkSql = "SELECT user1_id, user2_id FROM conversations WHERE id = " + std::to_string(conversationId);
    sqlite3_stmt* stmt;
    bool isParticipant = false;
    int recipientId = -1;
    
    if (sqlite3_prepare_v2(m_db, checkSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int u1 = sqlite3_column_int(stmt, 0);
            int u2 = sqlite3_column_int(stmt, 1);
            if (senderId == u1) {
                recipientId = u2;
                isParticipant = true;
            } else if (senderId == u2) {
                recipientId = u1;
                isParticipant = true;
            }
        }
        sqlite3_finalize(stmt);
    }
    
    if (!isParticipant) {
        LOG_ERROR("User " + std::to_string(senderId) + " is not a participant of conversation " + std::to_string(conversationId));
        return false;
    }

    std::string sql = "INSERT INTO messages (conversation_id, sender_id, text) VALUES (" +
                      std::to_string(conversationId) + ", " +
                      std::to_string(senderId) + ", '" +
                      escapeString(text) + "')";
    
    bool success = executeSQL(sql);
    if (success) {
        LOG_INFO("Message sent in conversation " + std::to_string(conversationId) + " by user " + std::to_string(senderId));
        

        auto sender = findUserById(senderId);
        if (sender && recipientId != -1) {
            std::string preview = text;
            if (preview.length() > 40) preview = preview.substr(0, 37) + "...";
            addNotification(recipientId, "Новое сообщение от " + sender->name + ": " + preview, "message");
        }
    }
    return success;
}

std::vector<Message> Database::getMessages(int conversationId) {
    std::string sql = "SELECT m.id, m.conversation_id, m.sender_id, u.name, m.text, m.timestamp "
                      "FROM messages m "
                      "JOIN users u ON u.id = m.sender_id "
                      "WHERE m.conversation_id = " + std::to_string(conversationId) + " "
                      "ORDER BY m.timestamp ASC";
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    std::vector<Message> result;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message msg;
        msg.id = sqlite3_column_int(stmt, 0);
        msg.sessionId = sqlite3_column_int(stmt, 1);
        msg.senderId = sqlite3_column_int(stmt, 2);
        msg.senderName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        msg.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        msg.timestamp = sqlite3_column_int64(stmt, 5);
        result.push_back(msg);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

bool Database::deleteConversation(int conversationId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    
    std::string deleteMsgs = "DELETE FROM messages WHERE conversation_id = " + std::to_string(conversationId);
    if (!executeSQL(deleteMsgs)) {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    
    std::string deleteConv = "DELETE FROM conversations WHERE id = " + std::to_string(conversationId);
    if (!executeSQL(deleteConv)) {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    
    sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr);
    LOG_INFO("Conversation " + std::to_string(conversationId) + " and all its messages deleted");
    return true;
}

void Database::autoUpdateSessionsLoop() {
    while (m_running) {

        for (int i = 0; i < 10 && m_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!m_running) break;
        
        long long currentTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            
        std::string sql = "UPDATE sessions SET status = 'completed' "
                          "WHERE status = 'scheduled' AND "
                          "slot_id IN (SELECT id FROM slots WHERE start_time + duration_minutes * 60 < " + std::to_string(currentTime) + ")";
        
        executeSQL(sql);
    }
}

bool Database::addNotification(int userId, const std::string& text, const std::string& type) {
    std::string sql = "INSERT INTO notifications (user_id, text, type) VALUES (" +
                      std::to_string(userId) + ", '" +
                      escapeString(text) + "', '" +
                      escapeString(type) + "')";
    return executeSQL(sql);
}

std::vector<Notification> Database::getNotifications(int userId, bool onlyUnread) {
    std::string sql = "SELECT id, user_id, text, type, is_read, created_at FROM notifications "
                      "WHERE user_id = " + std::to_string(userId);
    if (onlyUnread) {
        sql += " AND is_read = 0";
    }
    sql += " ORDER BY created_at DESC";
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sqlite3_stmt* stmt;
    std::vector<Notification> result;
    
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Notification n;
            n.id = sqlite3_column_int(stmt, 0);
            n.userId = sqlite3_column_int(stmt, 1);
            n.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            n.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            n.isRead = sqlite3_column_int(stmt, 4) != 0;
            n.createdAt = sqlite3_column_int64(stmt, 5);
            result.push_back(n);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

bool Database::markNotificationRead(int notificationId) {
    std::string sql = "UPDATE notifications SET is_read = 1 WHERE id = " + std::to_string(notificationId);
    return executeSQL(sql);
}

bool Database::deleteNotification(int notificationId) {
    std::string sql = "DELETE FROM notifications WHERE id = " + std::to_string(notificationId);
    return executeSQL(sql);
}
