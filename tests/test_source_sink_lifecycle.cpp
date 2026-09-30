#include <catch2/catch_test_macros.hpp>
#include "ISource.h"
#include "ISink.h"
#include <atomic>
#include <chrono>
#include <thread>

// ISource destruction must join its capture thread, and Toggle(false) must return promptly.

namespace {

class TestSource : public ISource {
public:
    TestSource(std::shared_ptr<Logger> logger, std::string id) : ISource(logger, id) {}
    std::atomic<int> captureCount{0};

protected:
    void CaptureFrame() override {
        captureCount++;
        SetLatestResult(SourceResult(std::nullopt, cv::Mat(1, 1, CV_8UC1), SourceResult::NowUs()));
        // Yield between frames rather than spin.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
};

class TestSink : public ISink {
public:
    TestSink(std::shared_ptr<Logger> logger, std::string id)
        : ISink(logger, /*maxSources*/ 1, /*requireJson*/ false, /*requireFrame*/ true, id) {}
    std::atomic<int> processedResultCount{0};

protected:
    void Process(const std::vector<SourceResult>& results) override {
        processedResultCount += static_cast<int>(results.size());
    }
};

} // namespace

TEST_CASE("a bound, toggled-on source/sink pair actually moves frames end to end", "[ISource][ISink]") {
    auto source = std::make_shared<TestSource>(nullptr, "test-source");
    auto sink = std::make_shared<TestSink>(nullptr, "test-sink");

    REQUIRE(sink->BindSource(source));

    source->Toggle(true);
    sink->Toggle(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    source->Toggle(false);
    sink->Toggle(false);

    REQUIRE(source->captureCount.load() > 0);
    REQUIRE(sink->processedResultCount.load() > 0);
}

TEST_CASE("ISource::Toggle(false) returns promptly instead of hanging", "[ISource][regression]") {
    auto source = std::make_shared<TestSource>(nullptr, "test-source-stop");
    source->Toggle(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto start = std::chrono::steady_clock::now();
    source->Toggle(false);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Generous bound: checks it does not hang, not timing.
    REQUIRE(elapsed < std::chrono::seconds(2));
    REQUIRE_FALSE(source->GetToggleStatus());
}

TEST_CASE("ISink::Toggle(false) returns promptly instead of hanging", "[ISink][regression]") {
    auto sink = std::make_shared<TestSink>(nullptr, "test-sink-stop");
    sink->Toggle(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto start = std::chrono::steady_clock::now();
    sink->Toggle(false);
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(elapsed < std::chrono::seconds(2));
    REQUIRE_FALSE(sink->GetToggleStatus());
}

TEST_CASE("a source destroyed while its capture thread is still running does not hang or crash", "[ISource][regression]") {
    // Destroying a toggled-on source joins its capture thread.
    auto start = std::chrono::steady_clock::now();
    {
        auto source = std::make_shared<TestSource>(nullptr, "test-source-destroy");
        source->Toggle(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // source goes out of scope here while still toggled on
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::seconds(2));
}

TEST_CASE("calling Toggle(true) twice does not leak a second capture thread", "[ISource][regression]") {
    // Toggle(true) while running must not spawn a second capture thread.
    auto source = std::make_shared<TestSource>(nullptr, "test-source-idempotent");
    source->Toggle(true);
    source->Toggle(true); // no-op
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto start = std::chrono::steady_clock::now();
    source->Toggle(false);
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(elapsed < std::chrono::seconds(2));
}

TEST_CASE("the FPS limit drops results published faster than it allows", "[ISource][fpslimit]") {
    auto source = std::make_shared<TestSource>(nullptr, "test-source-fps-limit");
    source->SetFpsLimit(20);
    REQUIRE(source->GetFpsLimit() == 20);
    source->Toggle(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    source->Toggle(false);

    const uint64_t published = source->GetCurrentFrameCount();
    // 20 fps over half a second is about 10 results; the source itself kept capturing at full rate
    REQUIRE(published >= 4);
    REQUIRE(published <= 13);
    REQUIRE(static_cast<uint64_t>(source->captureCount.load()) > published);
}

TEST_CASE("without a limit every result is published", "[ISource][fpslimit]") {
    auto source = std::make_shared<TestSource>(nullptr, "test-source-no-limit");
    REQUIRE(source->GetFpsLimit() <= 0);
    source->Toggle(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    source->Toggle(false);
    REQUIRE(source->GetCurrentFrameCount() == static_cast<uint64_t>(source->captureCount.load()));
}

TEST_CASE("a one-shot frame request counts as a frame consumer until a frame is published", "[ISource][snapshot]") {
    auto source = std::make_shared<TestSource>(nullptr, "test-source-frame-request");
    REQUIRE_FALSE(source->HasActiveFrameConsumer());
    REQUIRE_FALSE(source->HasActiveColorFrameConsumer());

    source->RequestFrameOnce();
    REQUIRE(source->HasActiveFrameConsumer());
    REQUIRE(source->HasActiveColorFrameConsumer());

    source->Toggle(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    source->Toggle(false);
    REQUIRE_FALSE(source->HasActiveFrameConsumer());
    REQUIRE_FALSE(source->HasActiveColorFrameConsumer());
}
