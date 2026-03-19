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
    std::shared_ptr<Database> m_db;
    std::string handleRegister(const boost::json::object& data);
    std::string handleLogin(const boost::json::object& data);
    std::string handleGetPsychologists(const boost::json::object& data);
    std::string handleGetFreeSlots(const boost::json::object& data);
    std::string handleBookSlot(const boost::json::object& data);
    std::string handleError(const std::string& message);
    
    boost::json::object parseRequest(const std::string& request);
    std::string serializeResponse(const boost::json::object& response);
};