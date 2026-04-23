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

TEST_F(DatabaseTest, DeleteFreeSlot) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    int slotId = slots[0].id;
    
    EXPECT_TRUE(db->deleteSlot(slotId, psyId));
    
    slots = db->getFreeSlots(psyId);
    EXPECT_EQ(slots.size(), 0);
}

TEST_F(DatabaseTest, CantDeleteBookedSlot) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    int slotId = slots[0].id;
    
    int sessionId = db->bookSlot(slotId, clientId);
    EXPECT_GT(sessionId, 0);
    
    EXPECT_FALSE(db->deleteSlot(slotId, psyId));
}

TEST_F(DatabaseTest, CantDeleteOtherPsychologistSlot) {
    User psy2;
    psy2.name = "Dr. Other";
    psy2.email = "other@example.com";
    psy2.passwordHash = hashPassword("pass123");
    psy2.role = UserRole::Psychologist;
    psy2.specialization = "CBT";
    psy2.education = "University";
    psy2.description = "Another psychologist";
    auto psy2Id = db->registerUser(psy2);
    ASSERT_TRUE(psy2Id.has_value());
    
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    int slotId = slots[0].id;
    
    EXPECT_FALSE(db->deleteSlot(slotId, psy2Id.value()));
    
    slots = db->getFreeSlots(psyId);
    EXPECT_EQ(slots.size(), 1);
}

TEST_F(DatabaseTest, GetSessionsByClientId) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    
    int sessionId = db->bookSlot(slots[0].id, clientId);
    EXPECT_GT(sessionId, 0);
    
    auto sessions = db->getSessionsByClientId(clientId);
    EXPECT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions[0].clientId, clientId);
    EXPECT_EQ(sessions[0].psychologistId, psyId);
}

TEST_F(DatabaseTest, GetSessionsByPsychologistId) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    
    int sessionId = db->bookSlot(slots[0].id, clientId);
    EXPECT_GT(sessionId, 0);
    
    auto sessions = db->getSessionsByPsychologistId(psyId);
    EXPECT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions[0].psychologistId, psyId);
    EXPECT_EQ(sessions[0].clientId, clientId);
}

TEST_F(DatabaseTest, GetSessionById) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    
    int sessionId = db->bookSlot(slots[0].id, clientId);
    EXPECT_GT(sessionId, 0);
    
    auto session = db->getSessionById(sessionId);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->id, sessionId);
    EXPECT_EQ(session->clientId, clientId);
    EXPECT_EQ(session->psychologistId, psyId);
    
    auto notFound = db->getSessionById(999);
    EXPECT_FALSE(notFound.has_value());
}

TEST_F(DatabaseTest, SendAndGetMessages) {
    int convId = db->getOrCreateConversation(clientId, psyId);
    
    EXPECT_TRUE(db->sendMessage(convId, clientId, "Message 1"));
    EXPECT_TRUE(db->sendMessage(convId, psyId, "Message 2"));
    EXPECT_TRUE(db->sendMessage(convId, clientId, "Message 3"));
    
    auto messages = db->getMessages(convId);
    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages[0].text, "Message 1");
    EXPECT_EQ(messages[1].text, "Message 2");
    EXPECT_EQ(messages[2].text, "Message 3");
}

TEST_F(DatabaseTest, CantSendMessageToNonexistentConversation) {
    EXPECT_FALSE(db->sendMessage(999, clientId, "Hello"));
}

TEST_F(DatabaseTest, CantSendMessageAsNonParticipant) {
    int convId = db->getOrCreateConversation(clientId, psyId);
    
    User other;
    other.name = "Other";
    other.email = "other@example.com";
    other.passwordHash = hashPassword("pass");
    other.role = UserRole::Client;
    auto otherId = db->registerUser(other);
    
    EXPECT_FALSE(db->sendMessage(convId, otherId.value(), "I shouldn't be here"));
    EXPECT_TRUE(db->sendMessage(convId, clientId, "Allowed"));
}

TEST_F(DatabaseTest, GetMessagesEmptySession) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    int sessionId = db->bookSlot(slots[0].id, clientId);
    ASSERT_GT(sessionId, 0);
    
    auto messages = db->getMessages(sessionId);
    EXPECT_EQ(messages.size(), 0);
}

TEST_F(DatabaseTest, UpdateUser) {
    User updated = db->findUserById(psyId).value();
    updated.name = "New Name";
    updated.specialization = "Expert";
    updated.education = "PhD";
    updated.description = "New Desc";
    
    ASSERT_TRUE(db->updateUser(updated));
    
    auto found = db->findUserById(psyId);
    EXPECT_EQ(found->name, "New Name");
    EXPECT_EQ(found->specialization, "Expert");
}

TEST_F(DatabaseTest, ConversationsAndMessages) {

    int convId = db->getOrCreateConversation(clientId, psyId);
    ASSERT_GT(convId, 0);
    

    int sameConvId = db->getOrCreateConversation(psyId, clientId);
    EXPECT_EQ(convId, sameConvId);
    

    EXPECT_TRUE(db->sendMessage(convId, clientId, "Hello Doc"));
    EXPECT_TRUE(db->sendMessage(convId, psyId, "Hi Client"));
    

    auto clientConvs = db->getConversations(clientId);
    ASSERT_EQ(clientConvs.size(), 1);
    EXPECT_EQ(clientConvs[0].otherUserId, psyId);
    

    auto messages = db->getMessages(convId);
    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages[0].text, "Hello Doc");
    EXPECT_EQ(messages[1].text, "Hi Client");
}

TEST_F(DatabaseTest, BookSlotCreatesConversation) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 3600, psyId);
    db->addSlot(slot);
    auto slots = db->getFreeSlots(psyId);
    int slotId = slots[0].id;
    

    int sessionId = db->bookSlot(slotId, clientId);
    ASSERT_GT(sessionId, 0);
    
    auto convs = db->getConversations(clientId);
    EXPECT_FALSE(convs.empty());
}

TEST_F(DatabaseTest, ConcurrentMessaging) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 86400, psyId);
    slot.durationMinutes = 60;
    slot.format = "online";
    db->addSlot(slot);
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    int sessionId = db->bookSlot(slots[0].id, clientId);
    ASSERT_GT(sessionId, 0);
    
    const int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, i]() {
            int senderId = (i % 2 == 0) ? clientId : psyId;
            std::string text = "Message " + std::to_string(i);
            if (db->sendMessage(sessionId, senderId, text)) {
                successCount++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(successCount, NUM_THREADS);
    
    auto messages = db->getMessages(sessionId);
    EXPECT_EQ(messages.size(), NUM_THREADS);
}

TEST_F(DatabaseTest, CancelAndEditSession) {
    Slot slot;
    slot.psychologistId = psyId;
    slot.startTime = std::chrono::system_clock::now() + std::chrono::hours(48);
    slot.durationMinutes = 60;
    slot.format = "online";
    EXPECT_TRUE(db->addSlot(slot));
    
    auto freeSlots = db->getFreeSlots(psyId);
    ASSERT_EQ(freeSlots.size(), 1);
    
    int sessionId = db->bookSlot(freeSlots[0].id, clientId);
    ASSERT_GT(sessionId, 0);
    
    time_t futureTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + std::chrono::hours(72));
    EXPECT_TRUE(db->updateSessionSlot(sessionId, futureTime, 90, "offline"));
    
    auto sessions = db->getSessionsByClientId(clientId);
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions[0].status, SessionStatus::Scheduled);
    
    EXPECT_TRUE(db->cancelSession(sessionId));
    sessions = db->getSessionsByClientId(clientId);
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions[0].status, SessionStatus::Cancelled);
}

TEST_F(DatabaseTest, NotificationsSystem) {
    EXPECT_TRUE(db->addNotification(clientId, "Test Info", "info"));
    EXPECT_TRUE(db->addNotification(clientId, "Test Warning", "warning"));
    
    auto all = db->getNotifications(clientId);
    EXPECT_EQ(all.size(), 2);
    
    bool hasInfo = false, hasWarning = false;
    for(const auto& n : all) {
        if (n.text == "Test Info") hasInfo = true;
        if (n.text == "Test Warning") hasWarning = true;
    }
    EXPECT_TRUE(hasInfo);
    EXPECT_TRUE(hasWarning);
    
    auto unread = db->getNotifications(clientId, true);
    EXPECT_EQ(unread.size(), 2);
    
    EXPECT_TRUE(db->markNotificationRead(all[0].id));
    unread = db->getNotifications(clientId, true);
    EXPECT_EQ(unread.size(), 1);
    
    EXPECT_TRUE(db->deleteNotification(all[0].id));
    all = db->getNotifications(clientId);
    EXPECT_EQ(all.size(), 1);
}

TEST_F(DatabaseTest, RebookingCancelledSlot) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 3600, psyId);
    db->addSlot(slot);
    auto slots = db->getFreeSlots(psyId);
    int slotId = slots[0].id;
    
    int session1 = db->bookSlot(slotId, clientId);
    ASSERT_GT(session1, 0);
    
    EXPECT_TRUE(db->cancelSession(session1));
    
    slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    EXPECT_EQ(slots[0].id, slotId);
    
    int session2 = db->bookSlot(slotId, clientId);
    EXPECT_GT(session2, 0);
    EXPECT_NE(session1, session2);
}

TEST_F(DatabaseTest, DeleteUserCascading) {
    Slot slot = Slot::fromTimeT(std::time(nullptr) + 3600, psyId);
    db->addSlot(slot);
    int slotId = db->getFreeSlots(psyId)[0].id;
    int sessionId = db->bookSlot(slotId, clientId);
    
    int convId = db->getOrCreateConversation(clientId, psyId);
    db->sendMessage(convId, clientId, "Hello");
    
    db->addNotification(clientId, "Note", "info");
    
    EXPECT_TRUE(db->deleteUser(clientId));
    
    EXPECT_FALSE(db->findUserById(clientId).has_value());
    
    auto slots = db->getFreeSlots(psyId);
    ASSERT_EQ(slots.size(), 1);
    EXPECT_EQ(slots[0].id, slotId);
    
    EXPECT_EQ(db->getNotifications(clientId).size(), 0);
    EXPECT_EQ(db->getMessages(convId).size(), 0);
}
