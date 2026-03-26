#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include "../src/network/Server.h"
#include "../src/database/Database.h"

class NetworkIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_shared<Database>(":memory:");
        db->initialize();
        
        User psychologist;
        psychologist.name = "Dr. Test";
        psychologist.email = "dr.test@example.com";
        psychologist.passwordHash = hashPassword("pass123");
        psychologist.role = UserRole::Psychologist;
        psychologist.specialization = "Family Therapy";
        psychologist.education = "University";
        psychologist.description = "Experienced";
        psyId = db->registerUser(psychologist).value_or(0);
        
        User client;
        client.name = "Test Client";
        client.email = "client@example.com";
        client.passwordHash = hashPassword("pass123");
        client.role = UserRole::Client;
        clientId = db->registerUser(client).value_or(0);
        
        server = std::make_unique<Server>(testPort, db, 1);
        serverThread = std::thread([this]() {
            server->run();
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void TearDown() override {
        server->stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
    }
    
    std::string sendRequest(const std::string& request) {
        try {
            boost::asio::io_context ioContext;
            boost::asio::ip::tcp::socket socket(ioContext);
            
            boost::asio::ip::tcp::endpoint endpoint(
                boost::asio::ip::make_address("127.0.0.1"), testPort);
            
            socket.connect(endpoint);
            
            std::string compactRequest;
            bool inString = false;
            for (char c : request) {
                if (c == '"') inString = !inString;
                if (!inString && (c == ' ' || c == '\n' || c == '\r' || c == '\t')) {
                    continue;
                }
                compactRequest += c;
            }
            
            if (compactRequest.back() != '\n') {
                compactRequest += '\n';
            }
            
            boost::asio::write(socket, boost::asio::buffer(compactRequest));
            
            boost::asio::streambuf buffer;
            boost::asio::read_until(socket, buffer, "\n");
            
            std::string response{
                boost::asio::buffers_begin(buffer.data()),
                boost::asio::buffers_end(buffer.data())
            };
            
            return response;
        } catch (const std::exception& e) {
            return "{\"status\":\"error\",\"message\":\"" + std::string(e.what()) + "\"}";
        }
    }
    
    std::shared_ptr<Database> db;
    std::unique_ptr<Server> server;
    std::thread serverThread;
    int psyId = 0;
    int clientId = 0;
    const short testPort = 12346;
};

TEST_F(NetworkIntegrationTest, RegisterClient) {
    std::string request = R"({
        "action": "register",
        "data": {
            "name": "New Client",
            "email": "newclient@test.com",
            "password": "pass123",
            "role": "client"
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
    EXPECT_TRUE(json["data"].as_object().contains("user_id"));
}

TEST_F(NetworkIntegrationTest, RegisterPsychologist) {
    std::string request = R"({
        "action": "register",
        "data": {
            "name": "New Psychologist",
            "email": "newpsy@test.com",
            "password": "pass123",
            "role": "psychologist",
            "specialization": "Clinical Psychology",
            "education": "PhD",
            "description": "Experienced professional"
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
}

TEST_F(NetworkIntegrationTest, Login) {
    std::string request = R"({
        "action": "login",
        "data": {
            "email": "client@example.com",
            "password": "pass123"
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
    EXPECT_EQ(json["data"].as_object()["user_id"].as_int64(), clientId);
}

TEST_F(NetworkIntegrationTest, GetPsychologists) {
    std::string request = R"({
        "action": "get_psychologists",
        "data": {}
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
    EXPECT_TRUE(json["data"].is_array());
}

TEST_F(NetworkIntegrationTest, GetFreeSlots) {
    Slot slot;
    slot.psychologistId = psyId;
    slot.startTime = std::chrono::system_clock::now() + std::chrono::hours(24);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    std::string request = R"({
        "action": "get_free_slots",
        "data": {
            "psychologist_id": )" + std::to_string(psyId) + R"(
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
    EXPECT_TRUE(json["data"].is_array());
}

TEST_F(NetworkIntegrationTest, BookSlot) {
    Slot slot;
    slot.psychologistId = psyId;
    slot.startTime = std::chrono::system_clock::now() + std::chrono::hours(24);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_FALSE(slots.empty());
    
    std::string request = R"({
        "action": "book_slot",
        "data": {
            "slot_id": )" + std::to_string(slots[0].id) + R"(,
            "client_id": )" + std::to_string(clientId) + R"(
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "success");
    EXPECT_TRUE(json["data"].as_object().contains("session_id"));
}

TEST_F(NetworkIntegrationTest, InvalidJson) {
    std::string request = "{invalid json";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
    EXPECT_EQ(json["code"], 400);
}

TEST_F(NetworkIntegrationTest, EmptyRequest) {
    std::string request = "";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
    EXPECT_EQ(json["code"], 400);
}

TEST_F(NetworkIntegrationTest, UnknownAction) {
    std::string request = R"({
        "action": "unknown_action",
        "data": {}
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
    EXPECT_EQ(json["code"], 400);
}

TEST_F(NetworkIntegrationTest, RegisterMissingFields) {
    std::string request = R"({
        "action": "register",
        "data": {
            "name": "Test",
            "role": "client"
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
    EXPECT_EQ(json["code"], 422);
    EXPECT_TRUE(json.contains("field_errors"));
}

TEST_F(NetworkIntegrationTest, RegisterPsychologistMissingFields) {
    std::string request = R"({
        "action": "register",
        "data": {
            "name": "Dr. Test",
            "email": "drtest@psy.com",
            "password": "pass",
            "role": "psychologist"
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
    EXPECT_EQ(json["code"], 422);
    EXPECT_TRUE(json["field_errors"].as_object().contains("specialization"));
    EXPECT_TRUE(json["field_errors"].as_object().contains("education"));
    EXPECT_TRUE(json["field_errors"].as_object().contains("description"));
}

TEST_F(NetworkIntegrationTest, DuplicateEmail) {
    std::string request = R"({
        "action": "register",
        "data": {
            "name": "Duplicate",
            "email": "client@example.com",
            "password": "pass123",
            "role": "client"
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
    EXPECT_EQ(json["code"], 409);
}

TEST_F(NetworkIntegrationTest, WrongPassword) {
    std::string request = R"({
        "action": "login",
        "data": {
            "email": "client@example.com",
            "password": "wrongpassword"
        }
    })";
    
    std::string response = sendRequest(request);
    auto json = boost::json::parse(response).as_object();
    
    EXPECT_EQ(json["status"], "error");
    EXPECT_EQ(json["code"], 401);
}