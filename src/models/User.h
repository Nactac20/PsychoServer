#pragma once
#include "UserRole.h"
#include <string>
#include <regex>

struct User {
    int id = 0;
    std::string name;
    std::string email;
    std::string passwordHash;
    UserRole role = UserRole::Client;
    
    std::string specialization;
    std::string education;
    std::string description;
    std::string photoPath;
    
    bool isPsychologist() const { return role == UserRole::Psychologist; }
    bool isClient() const { return role == UserRole::Client; }
    
    bool isValidEmail() const {
        const std::regex pattern(
            R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9][a-zA-Z0-9-]*(\.[a-zA-Z0-9][a-zA-Z0-9-]*)*\.[a-zA-Z]{2,}$)"
        );
        
        return std::regex_match(email, pattern);
    }
};

inline std::string hashPassword(const std::string& password) {
    return "hash_" + password;
}