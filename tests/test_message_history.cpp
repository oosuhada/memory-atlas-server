#include "../include/MessageHistory.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class MessageHistoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        history_dir_ = std::filesystem::temp_directory_path() / "memory-atlas-message-history-test";
        std::filesystem::remove_all(history_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(history_dir_);
    }

    std::filesystem::path history_dir_;
};

TEST_F(MessageHistoryTest, LimitedHistoryReturnsOnlyNewestEntriesInOrder) {
    MessageHistory history(history_dir_.string());
    ASSERT_TRUE(history.is_enabled());

    for (int index = 0; index < 50; ++index) {
        history.log_global_message("message-" + std::to_string(index), "tester");
    }

    const auto tail = history.load_global_history(5);
    ASSERT_EQ(tail.size(), 5U);
    EXPECT_NE(tail[0].find("message-45"), std::string::npos);
    EXPECT_NE(tail[4].find("message-49"), std::string::npos);
}

TEST_F(MessageHistoryTest, ZeroLimitPreservesReturnAllSemantics) {
    MessageHistory history(history_dir_.string());
    for (int index = 0; index < 12; ++index) {
        history.log_global_message("all-" + std::to_string(index), "tester");
    }

    const auto all = history.load_global_history(0);
    ASSERT_EQ(all.size(), 12U);
    EXPECT_NE(all.front().find("all-0"), std::string::npos);
    EXPECT_NE(all.back().find("all-11"), std::string::npos);
}

TEST_F(MessageHistoryTest, LimitLargerThanFileReturnsEveryEntry) {
    MessageHistory history(history_dir_.string());
    for (int index = 0; index < 3; ++index) {
        history.log_global_message("small-" + std::to_string(index), "tester");
    }

    const auto all = history.load_global_history(100);
    EXPECT_EQ(all.size(), 3U);
}

TEST_F(MessageHistoryTest, RoomNamesCannotEscapeRoomsDirectory) {
    MessageHistory history(history_dir_.string());
    history.log_room_message("../global/escaped", "escape-attempt", "tester");

    EXPECT_FALSE(std::filesystem::exists(history_dir_ / "global" / "escaped.txt"));
    EXPECT_TRUE(std::filesystem::exists(history_dir_ / "rooms" / "___global_escaped.txt"));
    const auto loaded = history.load_room_history("../global/escaped", 10);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_NE(loaded[0].find("escape-attempt"), std::string::npos);
}

TEST_F(MessageHistoryTest, PrivateHistorySanitizesUserIdentifiersConsistently) {
    MessageHistory history(history_dir_.string());
    history.log_private_message("private-message", "../alice", "bob/../../global");

    const auto loaded = history.load_private_history("../alice", "bob/../../global", 10);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_NE(loaded[0].find("private-message"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(history_dir_ / "global.txt"));
}

} // namespace
