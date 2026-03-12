#pragma once
#include <string>

enum class UserRole {
    Client,
    Psychologist
};

inline std::string userRoleToString(UserRole role) {
    switch(role) {
        case UserRole::Client: return "client";
        case UserRole::Psychologist: return "psychologist";
        default: return "unknown";
    }
}

inline UserRole userRoleFromString(const std::string& str) {
    if (str == "psychologist") return UserRole::Psychologist;
    return UserRole::Client;
}