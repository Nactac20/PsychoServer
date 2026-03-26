#pragma once
#include "ErrorCode.h"
#include <string>
#include <vector>

namespace error {
    struct FieldError {
        std::string field;
        std::string message;
        
        FieldError(const std::string& f, const std::string& m) : field(f), message(m) {}
    };

    class ErrorResponse {
    public:
        ErrorResponse(ErrorCode code, const std::string& message) : m_code(code), m_message(message) {}
        ErrorResponse(ErrorCode code, const std::string& message, const std::vector<FieldError>& fieldErrors) : m_code(code), m_message(message), m_fieldErrors(fieldErrors) {}
        
        ErrorCode code() const { return m_code; }
        std::string message() const { return m_message; }
        const std::vector<FieldError>& fieldErrors() const { return m_fieldErrors; }
        bool hasFieldErrors() const { return !m_fieldErrors.empty(); }
        
        ErrorResponse& addFieldError(const std::string& field, const std::string& message) {
            m_fieldErrors.emplace_back(field, message);
            return *this;
        }
        
        static ErrorResponse invalidRequest(const std::string& details = "") {
            return ErrorResponse(ErrorCode::InvalidRequest, 
                details.empty() ? "Invalid request format" : details);
        }
        
        static ErrorResponse validationError(const std::string& field, const std::string& reason) {
            ErrorResponse response(ErrorCode::ValidationError, "Validation failed");
            response.addFieldError(field, reason);
            return response;
        }
        
        static ErrorResponse validationError(const std::vector<FieldError>& errors) {
            return ErrorResponse(ErrorCode::ValidationError, "Validation failed", errors);
        }
        
        static ErrorResponse notFound(const std::string& resource) {
            return ErrorResponse(ErrorCode::NotFound, resource + " not found");
        }
        
        static ErrorResponse conflict(const std::string& details) {
            return ErrorResponse(ErrorCode::Conflict, details);
        }
        
        static ErrorResponse unauthorized(const std::string& details = "Invalid credentials") {
            return ErrorResponse(ErrorCode::Unauthorized, details);
        }
        
        static ErrorResponse internalError(const std::string& details = "") {
            return ErrorResponse(ErrorCode::InternalError,
                details.empty() ? "Internal server error" : details);
        }
        
        static ErrorResponse databaseError(const std::string& details = "") {
            return ErrorResponse(ErrorCode::DatabaseError,
                details.empty() ? "Database operation failed" : details);
        }

    private:
        ErrorCode m_code;
        std::string m_message;
        std::vector<FieldError> m_fieldErrors;
    };
} 