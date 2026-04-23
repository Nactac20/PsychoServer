#pragma once
#include <string>

namespace error {
    enum class ErrorCode {
        Success = 0,
        InvalidRequest = 400,
        Unauthorized = 401,
        Forbidden = 403,
        NotFound = 404,
        Conflict = 409,
        ValidationError = 422,
        InternalError = 500,
        DatabaseError = 503
    };

    inline std::string errorCodeToString(ErrorCode code) {
        switch (code) {
            case ErrorCode::InvalidRequest: return "Invalid request format";
            case ErrorCode::Unauthorized: return "Unauthorized";
            case ErrorCode::Forbidden: return "Forbidden";
            case ErrorCode::NotFound: return "Resource not found";
            case ErrorCode::Conflict: return "Resource conflict";
            case ErrorCode::ValidationError: return "Validation failed";
            case ErrorCode::InternalError: return "Internal server error";
            case ErrorCode::DatabaseError: return "Database error";
            default: return "Unknown error";
        }
    }

    inline int errorCodeToHttpStatus(ErrorCode code) {
        return static_cast<int>(code);
    }
}