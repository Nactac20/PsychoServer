#pragma once
#include "../database/Database.h"
#include <boost/json.hpp>
#include <string>
#include <memory>

class RequestHandler {
public:
    explicit RequestHandler(std::shared_ptr<Database> db);
    std::string handleRequest(const std::string& request);
    
private:
    std::string handleRegister(const boost::json::object& data);
    std::string handleLogin(const boost::json::object& data);
    std::string handleGetPsychologists(const boost::json::object& data);
    std::string handleGetFreeSlots(const boost::json::object& data);
    std::string handleBookSlot(const boost::json::object& data);
    std::string handleAddSlot(const boost::json::object& data);
    std::string handleEditSlot(const boost::json::object& data);
    std::string handleDeleteSlot(const boost::json::object& data);
    
    std::string handleUpdateUser(const boost::json::object& data);
    std::string handleDeleteUser(const boost::json::object& data);
    std::string handleGetConversations(const boost::json::object& data);
    std::string handleGetMessages(const boost::json::object& data);
    std::string handleSendMessage(const boost::json::object& data);
    std::string handleDeleteConversation(const boost::json::object& data);
    std::string handleGetMySessions(const boost::json::object& data);
    std::string handleGetSessionInfo(const boost::json::object& data);
    std::string handleCancelSession(const boost::json::object& data);
    std::string handleEditSession(const boost::json::object& data);
    
    std::string handleGetNotifications(const boost::json::object& data);
    std::string handleMarkNotificationRead(const boost::json::object& data);
    std::string handleDeleteNotification(const boost::json::object& data);
    
    std::string handleError(const std::string& message);
    boost::json::object parseRequest(const std::string& request);
    std::string serializeResponse(const boost::json::object& response);
    std::shared_ptr<Database> m_db;
};
