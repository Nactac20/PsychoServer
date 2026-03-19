#include "RequestHandler.h"
#include "../utils/Logger.h"
#include <boost/json/src.hpp>

RequestHandler::RequestHandler(std::shared_ptr<Database> db) : m_db(db) {}

std::string RequestHandler::handleRequest(const std::string& request) {
    try {
        auto json = parseRequest(request);
        std::string action = json["action"].as_string().c_str();
        auto data = json["data"].as_object();
        
        LOG_INFO("Received request: " + action);
        
        if (action == "register") return handleRegister(data);
        if (action == "login") return handleLogin(data);
        if (action == "get_psychologists") return handleGetPsychologists(data);
        if (action == "get_free_slots") return handleGetFreeSlots(data);
        if (action == "book_slot") return handleBookSlot(data);
        
        return handleError("Unknown action: " + action);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Request parsing error: " + std::string(e.what()));
        return handleError("Invalid request format");
    }
}

std::string RequestHandler::handleRegister(const boost::json::object& data) {
    User user;
    user.name = data.at("name").as_string().c_str();
    user.email = data.at("email").as_string().c_str();
    user.passwordHash = hashPassword(data.at("password").as_string().c_str());
    user.role = (data.at("role").as_string() == "psychologist") 
                ? UserRole::Psychologist : UserRole::Client;
    
    if (user.role == UserRole::Psychologist) {
        user.specialization = data.at("specialization").as_string().c_str();
        user.education = data.at("education").as_string().c_str();
        user.description = data.at("description").as_string().c_str();
    }
    
    auto id = m_db->registerUser(user);
    
    boost::json::object response;
    response["status"] = id.has_value() ? "success" : "error";
    
    if (id.has_value()) {
        response["data"] = {{"user_id", *id}};
    } else {
        response["message"] = "Registration failed";
    }
    
    return serializeResponse(response);
}

std::string RequestHandler::handleLogin(const boost::json::object& data) {
    std::string email = data.at("email").as_string().c_str();
    std::string password = data.at("password").as_string().c_str();
    
    bool valid = m_db->validatePassword(email, password);
    auto user = m_db->findUserByEmail(email);
    
    boost::json::object response;
    
    if (valid && user.has_value()) {
        response["status"] = "success";
        response["data"] = {
            {"user_id", user->id},
            {"role", user->role == UserRole::Client ? "client" : "psychologist"},
            {"name", user->name}
        };
    } else {
        response["status"] = "error";
        response["message"] = "Invalid email or password";
    }
    
    return serializeResponse(response);
}

std::string RequestHandler::handleGetPsychologists(const boost::json::object& data) {
    auto psychologists = m_db->getAllPsychologists();
    
    boost::json::array result;
    for (const auto& psy : psychologists) {
        result.push_back({
            {"id", psy.id},
            {"name", psy.name},
            {"specialization", psy.specialization},
            {"education", psy.education},
            {"description", psy.description}
        });
    }
    
    boost::json::object response;
    response["status"] = "success";
    response["data"] = result;
    
    return serializeResponse(response);
}

std::string RequestHandler::handleGetFreeSlots(const boost::json::object& data) {
    int psychologistId = data.at("psychologist_id").as_int64();
    auto slots = m_db->getFreeSlots(psychologistId);
    
    boost::json::array result;
    for (const auto& slot : slots) {
        result.push_back({
            {"slot_id", slot.id},
            {"start_time", static_cast<long long>(slot.toTimeT())},
            {"duration", slot.durationMinutes},
            {"format", slot.format}
        });
    }
    
    boost::json::object response;
    response["status"] = "success";
    response["data"] = result;
    
    return serializeResponse(response);
}

std::string RequestHandler::handleBookSlot(const boost::json::object& data) {
    int slotId = data.at("slot_id").as_int64();
    int clientId = data.at("client_id").as_int64();
    
    int sessionId = m_db->bookSlot(slotId, clientId);
    
    boost::json::object response;
    
    if (sessionId > 0) {
        response["status"] = "success";
        response["data"] = {{"session_id", sessionId}};
    } else {
        response["status"] = "error";
        response["message"] = "Booking failed";
    }
    
    return serializeResponse(response);
}

std::string RequestHandler::handleError(const std::string& message) {
    boost::json::object response;
    response["status"] = "error";
    response["message"] = message;
    return serializeResponse(response);
}

boost::json::object RequestHandler::parseRequest(const std::string& request) {
    auto value = boost::json::parse(request);
    return value.as_object();
}

std::string RequestHandler::serializeResponse(const boost::json::object& response) {
    return boost::json::serialize(response) + "\n";
}