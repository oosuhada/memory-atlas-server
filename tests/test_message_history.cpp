#include "../include/MessageHistory.hpp"

#include <gtest/gtest.h>

#include <filesystem>
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

} // namespace
