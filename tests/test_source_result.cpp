#include <catch2/catch_test_macros.hpp>
#include "SourceResult.h"

TEST_CASE("SourceResult::NowUs is a plausible, monotonically non-decreasing wall clock", "[SourceResult]") {
    uint64_t first = SourceResult::NowUs();
    uint64_t second = SourceResult::NowUs();
    // Loose sanity floor: catches a zero or a millisecond/nanosecond value instead of microseconds.
    REQUIRE(first > 1'700'000'000'000'000ULL);
    REQUIRE(second >= first);
}

TEST_CASE("SourceResult's two-argument constructor leaves captureTimeUs at its honest-unknown default", "[SourceResult]") {
    // The zero-to-producedTimeUs substitution is done by ISource::SetLatestResult, not the constructor.
    SourceResult result(std::nullopt, cv::Mat());
    REQUIRE(result.captureTimeUs == 0);
    REQUIRE(result.frameNumber == 0);
}

TEST_CASE("SourceResult's three-argument constructor records an explicit capture timestamp", "[SourceResult]") {
    SourceResult result(std::nullopt, cv::Mat(), 12345ULL);
    REQUIRE(result.captureTimeUs == 12345ULL);
}

TEST_CASE("SourceResult round-trips optional json and frame fields", "[SourceResult]") {
    // A bare cv::Mat becomes a BGR24 Frame; pixel access goes through Frame::AsBgr().
    nlohmann::json j = {{"id", 7}};
    cv::Mat m(4, 4, CV_8UC3, cv::Scalar(42, 42, 42));
    SourceResult result(j, m);

    REQUIRE(result.json.has_value());
    REQUIRE((*result.json)["id"] == 7);
    REQUIRE(result.frame.has_value());
    REQUIRE(result.frame->AsBgr().at<cv::Vec3b>(0, 0) == cv::Vec3b(42, 42, 42));
}
