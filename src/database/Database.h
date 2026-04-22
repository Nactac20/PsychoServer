#pragma once
#include "../models/User.h"
#include "../models/Slot.h"
#include "../models/Session.h"
#include "../models/Message.h"

struct Conversation {
    int id;
    int otherUserId;
    std::string otherUserName;
    std::string lastMessage;
};

struct Notification {
    int id;
    int userId;
    std::string text;
    std::string type;
    bool isRead;
    long long createdAt;
};
#include "../utils/Logger.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

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
    bool editSlot(int slotId, long long startTime, int duration, const std::string& format);
    std::vector<Slot> getFreeSlots(int psychologistId);
    
    int bookSlot(int slotId, int clientId);
    bool updateUser(const User& user);
    bool deleteUser(int userId);
    
    int getOrCreateConversation(int user1Id, int user2Id);
    std::vector<Conversation> getConversations(int userId);
    bool sendMessage(int conversationId, int senderId, const std::string& text);
    std::vector<Message> getMessages(int conversationId);
    bool deleteConversation(int conversationId);
    
    bool deleteSlot(int slotId, int psychologistId);
    
    std::vector<Session> getSessionsByClientId(int clientId);
    std::vector<Session> getSessionsByPsychologistId(int psychologistId);
    std::optional<Session> getSessionById(int sessionId);
    bool cancelSession(int sessionId);
    bool updateSessionSlot(int sessionId, long long startTime, int duration, const std::string& format);
    
    bool addNotification(int userId, const std::string& text, const std::string& type = "info");
    std::vector<Notification> getNotifications(int userId, bool onlyUnread = false);
    bool markNotificationRead(int notificationId);
    bool deleteNotification(int notificationId);
    
private:
    bool executeSQL(const std::string& sql);
    std::string escapeString(const std::string& str);
    
    sqlite3* m_db = nullptr;
    std::string m_dbPath;
    std::recursive_mutex m_mutex;
    
    void autoUpdateSessionsLoop();
    std::thread m_workerThread;
    std::atomic<bool> m_running;
};
