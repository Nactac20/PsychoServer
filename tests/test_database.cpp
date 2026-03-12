#include <gtest/gtest.h>
#include "../src/database/Database.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_unique<Database>(":memory:");
        ASSERT_TRUE(db->initialize());
        
        User psy;
        psy.name = "Dr. Test";
        psy.email = "dr.test@example.com";
        psy.passwordHash = hashPassword("pass123");
        psy.role = UserRole::Psychologist;
        psy.specialization = "Family Therapy";
        psy.education = "University";
        psy.description = "Experienced";
        
        auto result = db->registerUser(psy);
        ASSERT_TRUE(result.has_value());
        psyId = result.value();
        
        User client;
        client.name = "Test Client";
        client.email = "client@example.com";
        client.passwordHash = hashPassword("pass123");
        client.role = UserRole::Client;
        
        result = db->registerUser(client);
        ASSERT_TRUE(result.has_value());
        clientId = result.value();
    }
    
    std::unique_ptr<Database> db;
    int psyId = 0;
    int clientId = 0;
};

TEST_F(DatabaseTest, RegisterAndFindUser) {
    auto found = db->findUserByEmail("dr.test@example.com");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Dr. Test");
    EXPECT_TRUE(found->isPsychologist());
}

TEST_F(DatabaseTest, FindUserById) {
    auto found = db->findUserById(psyId);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->email, "dr.test@example.com");
    
    auto notFound = db->findUserById(999);
    EXPECT_FALSE(notFound.has_value());
}

TEST_F(DatabaseTest, FindNonExistentUser) {
    auto found = db->findUserByEmail("nonexistent@example.com");
    EXPECT_FALSE(found.has_value());
}

TEST_F(DatabaseTest, ValidatePassword) {
    EXPECT_TRUE(db->validatePassword("dr.test@example.com", "pass123"));
    EXPECT_FALSE(db->validatePassword("dr.test@example.com", "wrong"));
    EXPECT_FALSE(db->validatePassword("wrong@example.com", "pass123"));
}

TEST_F(DatabaseTest, CantRegisterDuplicateEmail) {
    User duplicate;
    duplicate.name = "Another";
    duplicate.email = "dr.test@example.com";
    duplicate.passwordHash = hashPassword("pass");
    duplicate.role = UserRole::Client;
    
    auto id = db->registerUser(duplicate);
    EXPECT_FALSE(id.has_value());
}

TEST_F(DatabaseTest, AddAndGetSlots) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    
    EXPECT_TRUE(db->addSlot(slot));
    
    auto slots = db->getFreeSlots(psyId);
    EXPECT_EQ(slots.size(), 1);
    EXPECT_EQ(slots[0].psychologistId, psyId);
}

TEST_F(DatabaseTest, DontShowExpiredSlots) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) - 86400, psyId);
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    EXPECT_EQ(slots.size(), 0);
}

TEST_F(DatabaseTest, BookSlot) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    
    int sessionId = db->bookSlot(slots[0].id, clientId);
    EXPECT_GT(sessionId, 0);
    
    slots = db->getFreeSlots(psyId);
    EXPECT_EQ(slots.size(), 0);
}

TEST_F(DatabaseTest, CantBookSameSlotTwice) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    int slotId = slots[0].id;
    
    int session1 = db->bookSlot(slotId, clientId);
    EXPECT_GT(session1, 0);
    
    int session2 = db->bookSlot(slotId, clientId);
    EXPECT_EQ(session2, 0);
}

TEST_F(DatabaseTest, CantBookWithNonexistentClient) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    
    int sessionId = db->bookSlot(slots[0].id, 999);
    EXPECT_EQ(sessionId, 0);
}

TEST_F(DatabaseTest, CantBookExpiredSlot) {
    Slot expiredSlot = Slot::fromTimeT(std::time(nullptr) - 86400, psyId);
    db->addSlot(expiredSlot);
    
    Slot futureSlot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    db->addSlot(futureSlot);
    
    auto freeSlots = db->getFreeSlots(psyId);
    ASSERT_EQ(freeSlots.size(), 1);
    EXPECT_GT(freeSlots[0].toTimeT(), std::time(nullptr));
    
    int expiredSlotId = 1;
    int sessionId = db->bookSlot(expiredSlotId, clientId);
    EXPECT_EQ(sessionId, 0);
}

TEST_F(DatabaseTest, CantBookWithNonexistentPsychologist) {
    Slot slot;
    slot.psychologistId = 999;
    slot.startTime = std::chrono::system_clock::now() + std::chrono::hours(24);
    slot.durationMinutes = 60;
    slot.format = "online";
    
    bool added = db->addSlot(slot);
    EXPECT_FALSE(added);
    
    auto freeSlots = db->getFreeSlots(999);
    EXPECT_EQ(freeSlots.size(), 0);
    
    int sessionId = db->bookSlot(1, clientId);
    EXPECT_EQ(sessionId, 0);
}

TEST_F(DatabaseTest, ConcurrentBooking) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    int slotId = slots[0].id;
    
    const int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    
    std::vector<int> testClientIds;
    for (int i = 0; i < NUM_THREADS; i++) {
        User testClient;
        testClient.name = "Test Client " + std::to_string(i);
        testClient.email = "test.client" + std::to_string(i) + "@example.com";
        testClient.passwordHash = hashPassword("pass");
        testClient.role = UserRole::Client;
        
        auto id = db->registerUser(testClient);
        ASSERT_TRUE(id.has_value());
        testClientIds.push_back(id.value());
    }
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, i]() {
            int sessionId = db->bookSlot(slotId, testClientIds[i]);
            if (sessionId > 0) {
                successCount++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(successCount, 1);
}

TEST(EmailValidationTest, ValidEmails) {
    User user;
    
    std::vector<std::string> validEmails = {
        "simple@example.com",
        "very.common@example.com",
        "disposable.style.email.with+symbol@example.com",
        "other.email-with-dash@example.com",
        "x@example.com",
        "example@s.solutions",
        "user@subdomain.example.com",
        "user@example.co.ru",
        "firstname.lastname@example.com",
        "email@example-one.com",
        "email@example.name"
    };
    
    for (const auto& email : validEmails) {
        user.email = email;
        EXPECT_TRUE(user.isValidEmail()) << "Email should be valid: " << email;
    }
}

TEST(EmailValidationTest, InvalidEmails) {
    User user;
    
    std::vector<std::string> invalidEmails = {
        "plainaddress",
        "@missingusername.com",
        "username@.com",
        "username@.com.",
        "username@com",
        "username@-example.com",
        "username@example..com",
        "username@.example.com",
        "user name@example.com",
        "user@example.c",
        "user@example.",
        "user@.example.com",
        "user@example..com",
        "user@[192.168.2.1]",
        "user@localhost"
    };
    
    for (const auto& email : invalidEmails) {
        user.email = email;
        EXPECT_FALSE(user.isValidEmail()) << "Email should be invalid: " << email;
    }
}