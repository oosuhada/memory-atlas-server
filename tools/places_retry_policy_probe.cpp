#include "PlacesRetryPolicy.hpp"

#include <iostream>
#include <string>
#include <vector>

struct HttpScenario { const char* name; int status; bool expected; };

int main() {
    const std::vector<HttpScenario> http_scenarios = {
        {"timeout", 408, true},
        {"rate-limited", 429, true},
        {"server-error", 503, true},
        {"bad-request", 400, false},
        {"unauthorized", 401, false},
    };

    int passed = 0;
    for (const auto& scenario : http_scenarios) {
        if (PlacesRetryPolicy::is_retryable_http_status(scenario.status) == scenario.expected) ++passed;
    }
    const bool timeout_retry = PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::timed_out);
    const bool reset_retry = PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::connection_reset);
    const bool cancelled_retry = PlacesRetryPolicy::is_retryable_network_failure(PlacesNetworkFailure::operation_aborted);
    passed += timeout_retry ? 1 : 0;
    passed += reset_retry ? 1 : 0;
    passed += !cancelled_retry ? 1 : 0;

    const int total = static_cast<int>(http_scenarios.size()) + 3;
    std::cout << "{\n"
              << "  \"experiment\": \"memory-atlas-places-retry-policy-v1\",\n"
              << "  \"http_scenarios\": " << http_scenarios.size() << ",\n"
              << "  \"network_scenarios\": 3,\n"
              << "  \"passed\": " << passed << ",\n"
              << "  \"total\": " << total << ",\n"
              << "  \"max_attempts\": " << PlacesRetryPolicy::max_attempts << ",\n"
              << "  \"backoff_ms\": ["
              << PlacesRetryPolicy::backoff_for_attempt(0).count() << ", "
              << PlacesRetryPolicy::backoff_for_attempt(1).count() << ", "
              << PlacesRetryPolicy::backoff_for_attempt(2).count() << "],\n"
              << "  \"limitations\": [\"Policy probe injects failure classes without calling Google Places; it verifies retry boundaries, not upstream availability.\"]\n"
              << "}\n";
    return passed == total ? 0 : 1;
}
