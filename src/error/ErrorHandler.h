#pragma once
#include "ErrorResponse.h"
#include <boost/json.hpp>
#include <string>

namespace error {
    class ErrorHandler {
    public:
        static std::string toJson(const ErrorResponse& error) {
            boost::json::object response;
            response["status"] = "error";
            response["code"] = static_cast<int>(error.code());
            response["message"] = error.message();
            
            if (error.hasFieldErrors()) {
                boost::json::object fieldErrors;
                for (const auto& fieldErr : error.fieldErrors()) {
                    fieldErrors[fieldErr.field] = fieldErr.message;
                }
                response["field_errors"] = fieldErrors;
            }
            
            return boost::json::serialize(response) + "\n";
        }
        
        static std::string invalidRequest(const std::string& details = "") {
            return toJson(ErrorResponse::invalidRequest(details));
        }
        
        static std::string validationError(const std::string& field, const std::string& reason) {
            return toJson(ErrorResponse::validationError(field, reason));
        }
        
        static std::string validationError(const std::vector<FieldError>& errors) {
            return toJson(ErrorResponse::validationError(errors));
        }
        
        static std::string notFound(const std::string& resource) {
            return toJson(ErrorResponse::notFound(resource));
        }
        
        static std::string conflict(const std::string& details) {
            return toJson(ErrorResponse::conflict(details));
        }
        
        static std::string unauthorized(const std::string& details = "") {
            return toJson(ErrorResponse::unauthorized(details));
        }
        
        static std::string internalError(const std::string& details = "") {
            return toJson(ErrorResponse::internalError(details));
        }
        
        static std::string databaseError(const std::string& details = "") {
            return toJson(ErrorResponse::databaseError(details));
        }
        
        static std::string createErrorResponse(const ErrorResponse& error) {
            return toJson(error);
        }
    };
} 

using ErrorCode = error::ErrorCode;
using FieldError = error::FieldError;
using ErrorResponse = error::ErrorResponse;
using ErrorHandler = error::ErrorHandler;