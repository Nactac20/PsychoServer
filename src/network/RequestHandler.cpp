#include "RequestHandler.h"
#include "../utils/Logger.h"
#include "../error/ErrorHandler.h"
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#include <boost/system/system_error.hpp>

RequestHandler::RequestHandler(std::shared_ptr<Database> db) : m_db(db) {}

std::string RequestHandler::handleRequest(const std::string& request) {
    if (request.empty()) {
        LOG_WARNING("Empty request received");
        return ErrorHandler::invalidRequest("Empty request");
    }
    
    try {
        auto json = parseRequest(request);
        
        if (!json.contains("action")) {
            return ErrorHandler::invalidRequest("Missing 'action' field");
        }
        
        std::string action = json["action"].as_string().c_str();
        
        if (!json.contains("data")) {
            return ErrorHandler::invalidRequest("Missing 'data' field");
        }
        
        auto data = json["data"].as_object();
        LOG_INFO("Received request: " + action);
        
        if (action == "register") return handleRegister(data);
        if (action == "login") return handleLogin(data);
        if (action == "get_psychologists") return handleGetPsychologists(data);
        if (action == "get_free_slots") return handleGetFreeSlots(data);
        if (action == "book_slot") return handleBookSlot(data);
        
        return ErrorHandler::invalidRequest("Unknown action: " + action);
        
    } catch (const boost::system::system_error& e) {
        LOG_ERROR("JSON parse error: " + std::string(e.what()));
        return ErrorHandler::invalidRequest("Invalid JSON format");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Request handling error: " + std::string(e.what()));
        return ErrorHandler::internalError();
    }
}

std::string RequestHandler::handleRegister(const boost::json::object& data) {
    std::vector<FieldError> fieldErrors;
    
    if (!data.contains("name") || data.at("name").as_string().empty()) {
        fieldErrors.emplace_back("name", "Name is required");
    }
    if (!data.contains("email") || data.at("email").as_string().empty()) {
        fieldErrors.emplace_back("email", "Email is required");
    }
    if (!data.contains("password") || data.at("password").as_string().empty()) {
        fieldErrors.emplace_back("password", "Password is required");
    }
    if (!data.contains("role") || data.at("role").as_string().empty()) {
        fieldErrors.emplace_back("role", "Role is required");
    }
    
    if (!fieldErrors.empty()) {
        return ErrorHandler::validationError(fieldErrors);
    }
    
    User user;
    user.name = data.at("name").as_string().c_str();
    user.email = data.at("email").as_string().c_str();
    user.passwordHash = hashPassword(data.at("password").as_string().c_str());
    
    std::string roleStr = data.at("role").as_string().c_str();
    user.role = (roleStr == "psychologist") ? UserRole::Psychologist : UserRole::Client;
    
    if (user.role == UserRole::Psychologist) {
        if (!data.contains("specialization") || data.at("specialization").as_string().empty()) {
            fieldErrors.emplace_back("specialization", "Specialization is required for psychologist");
        }
        if (!data.contains("education") || data.at("education").as_string().empty()) {
            fieldErrors.emplace_back("education", "Education is required for psychologist");
        }
        if (!data.contains("description") || data.at("description").as_string().empty()) {
            fieldErrors.emplace_back("description", "Description is required for psychologist");
        }
        
        if (!fieldErrors.empty()) {
            return ErrorHandler::validationError(fieldErrors);
        }
        
        user.specialization = data.at("specialization").as_string().c_str();
        user.education = data.at("education").as_string().c_str();
        user.description = data.at("description").as_string().c_str();
        
        if (data.contains("photo_path")) {
            user.photoPath = data.at("photo_path").as_string().c_str();
        }
    }
    
    if (!user.isValidEmail()) {
        return ErrorHandler::validationError("email", "Invalid email format");
    }
    
    auto id = m_db->registerUser(user);
    
    if (id.has_value()) {
        boost::json::object response;
        response["status"] = "success";
        response["data"] = {{"user_id", *id}};
        return serializeResponse(response);
    } else {
        return ErrorHandler::conflict("User with this email already exists");
    }
}

std::string RequestHandler::handleLogin(const boost::json::object& data) {
    std::vector<FieldError> fieldErrors;
    
    if (!data.contains("email") || data.at("email").as_string().empty()) {
        fieldErrors.emplace_back("email", "Email is required");
    }
    if (!data.contains("password") || data.at("password").as_string().empty()) {
        fieldErrors.emplace_back("password", "Password is required");
    }
    
    if (!fieldErrors.empty()) {
        return ErrorHandler::validationError(fieldErrors);
    }
    
    std::string email = data.at("email").as_string().c_str();
    std::string password = data.at("password").as_string().c_str();
    
    bool valid = m_db->validatePassword(email, password);
    auto user = m_db->findUserByEmail(email);
    
    if (valid && user.has_value()) {
        boost::json::object response;
        response["status"] = "success";
        response["data"] = {
            {"user_id", user->id},
            {"role", user->role == UserRole::Client ? "client" : "psychologist"},
            {"name", user->name}
        };
        return serializeResponse(response);
    }
    
    return ErrorHandler::unauthorized("Invalid email or password");
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
    if (!data.contains("psychologist_id")) {
        return ErrorHandler::invalidRequest("Missing 'psychologist_id' field");
    }
    
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
    std::vector<FieldError> fieldErrors;
    
    if (!data.contains("slot_id")) {
        fieldErrors.emplace_back("slot_id", "slot_id is required");
    }
    if (!data.contains("client_id")) {
        fieldErrors.emplace_back("client_id", "client_id is required");
    }
    
    if (!fieldErrors.empty()) {
        return ErrorHandler::validationError(fieldErrors);
    }
    
    int slotId = data.at("slot_id").as_int64();
    int clientId = data.at("client_id").as_int64();
    
    int sessionId = m_db->bookSlot(slotId, clientId);
    
    if (sessionId > 0) {
        boost::json::object response;
        response["status"] = "success";
        response["data"] = {{"session_id", sessionId}};
        return serializeResponse(response);
    }
    
    return ErrorHandler::conflict("Slot is already booked or invalid");
}

std::string RequestHandler::handleError(const std::string& message) {
    return ErrorHandler::invalidRequest(message);
}

boost::json::object RequestHandler::parseRequest(const std::string& request) {
    auto value = boost::json::parse(request);
    return value.as_object();
}

std::string RequestHandler::serializeResponse(const boost::json::object& response) {
    return boost::json::serialize(response) + "\n";
}