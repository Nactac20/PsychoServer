#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include "../src/network/RequestHandler.h"

class NetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_shared<Database>(":memory:");
        db->initialize();
        handler = std::make_shared<RequestHandler>(db);
    }
    
    std::shared_ptr<Database> db;
    std::shared_ptr<RequestHandler> handler;
};

TEST_F(NetworkTest, HandleRegister) {
    std::string request = R"({
        "action": "register",
        "data": {
            "name": "Test User",
            "email": "test@test.com",
            "password": "pass",
            "role": "client"
        }
    })";
    
    auto response = handler->handleRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
    EXPECT_TRUE(json["data"].as_object().contains("user_id"));
}

TEST_F(NetworkTest, HandleLogin) {
    std::string registerReq = R"({
        "action": "register",
        "data": {
            "name": "Test User",
            "email": "login@test.com",
            "password": "pass123",
            "role": "client"
        }
    })";
    handler->handleRequest(registerReq);
    
    std::string loginReq = R"({
        "action": "login",
        "data": {
            "email": "login@test.com",
            "password": "pass123"
        }
    })";
    
    auto response = handler->handleRequest(loginReq);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
}

TEST_F(NetworkTest, HandleInvalidAction) {
    std::string request = R"({
        "action": "invalid_action",
        "data": {}
    })";
    
    auto response = handler->handleRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
}