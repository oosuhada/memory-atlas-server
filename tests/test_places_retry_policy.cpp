#include "PlacesRetryPolicy.hpp"

#include <gtest/gtest.h>

TEST(PlacesRetryPolicyTest, RetriesOnlyTransientHttpStatuses) {
    EXPECT_TRUE(PlacesRetryPolicy::is_retryable_http_status(408));
    EXPECT_TRUE(PlacesRetryPolicy::is_retryable_http_status(429));
    EXPECT_TRUE(PlacesRetryPolicy::is_retryable_http_status(500));
    EXPECT_TRUE(PlacesRetryPolicy::is_retryable_http_status(503));
    EXPECT_FALSE(PlacesRetryPolicy::is_retryable_http_status(400));
    EXPECT_FALSE(PlacesRetryPolicy::is_retryable_http_status(401));
    EXPECT_FALSE(PlacesRetryPolicy::is_retryable_http_status(404));
}

TEST(PlacesRetryPolicyTest, ClassifiesTransientNetworkFailures) {
    EXPECT_TRUE(PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::timed_out));
    EXPECT_TRUE(PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::connection_reset));
    EXPECT_TRUE(PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::network_unreachable));
    EXPECT_FALSE(PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::operation_aborted));
    EXPECT_FALSE(PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::other));
}

TEST(PlacesRetryPolicyTest, UsesBoundedExponentialBackoff) {
    EXPECT_EQ(PlacesRetryPolicy::backoff_for_attempt(0).count(), 100);
    EXPECT_EQ(PlacesRetryPolicy::backoff_for_attempt(1).count(), 200);
    EXPECT_EQ(PlacesRetryPolicy::backoff_for_attempt(2).count(), 400);
    EXPECT_EQ(PlacesRetryPolicy::backoff_for_attempt(10).count(), 1600);
}
