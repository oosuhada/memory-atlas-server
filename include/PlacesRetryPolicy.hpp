#pragma once

#include <chrono>

enum class PlacesNetworkFailure {
    address_not_available,
    connection_aborted,
    connection_refused,
    connection_reset,
    host_unreachable,
    network_down,
    network_reset,
    network_unreachable,
    timed_out,
    try_again,
    operation_aborted,
    other,
};

struct PlacesRetryPolicy {
    static constexpr int max_attempts = 3;

    static bool is_retryable_http_status(int status) {
        return status == 408 || status == 429 || (status >= 500 && status <= 599);
    }

    static bool is_retryable_network_failure(PlacesNetworkFailure failure) {
        switch (failure) {
            case PlacesNetworkFailure::address_not_available:
            case PlacesNetworkFailure::connection_aborted:
            case PlacesNetworkFailure::connection_refused:
            case PlacesNetworkFailure::connection_reset:
            case PlacesNetworkFailure::host_unreachable:
            case PlacesNetworkFailure::network_down:
            case PlacesNetworkFailure::network_reset:
            case PlacesNetworkFailure::network_unreachable:
            case PlacesNetworkFailure::timed_out:
            case PlacesNetworkFailure::try_again:
                return true;
            case PlacesNetworkFailure::operation_aborted:
            case PlacesNetworkFailure::other:
                return false;
        }
        return false;
    }

    static std::chrono::milliseconds backoff_for_attempt(int completed_attempts) {
        const int exponent = completed_attempts < 0 ? 0 : (completed_attempts > 4 ? 4 : completed_attempts);
        return std::chrono::milliseconds(100 * (1 << exponent));
    }
};
