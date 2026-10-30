#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "SystemMonitor.h"

namespace {

// A fake /sys/class/thermal with the given (type, millidegrees) zones; a negative temperature leaves the zone without a temp file.
struct FakeThermal {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "lumen-thermal-test";
    int count = 0;

    FakeThermal() { std::filesystem::remove_all(root); std::filesystem::create_directories(root); }
    ~FakeThermal() { std::filesystem::remove_all(root); }

    void Add(const std::string& type, int milliC, bool withTemp = true) {
        const auto zone = root / ("thermal_zone" + std::to_string(count++));
        std::filesystem::create_directories(zone);
        std::ofstream(zone / "type") << type << "\n";
        if (withTemp) std::ofstream(zone / "temp") << milliC << "\n";
    }
    int coolingCount = 0;
    void AddCooling(const std::string& type, int state) {
        const auto device = root / ("cooling_device" + std::to_string(coolingCount++));
        std::filesystem::create_directories(device);
        std::ofstream(device / "type") << type << "\n";
        std::ofstream(device / "cur_state") << state << "\n";
    }
    std::string Path() const { return root.string(); }
};

} // namespace

TEST_CASE("the CPU temperature comes from the SoC zone, not just the hottest one", "[system][temperature]") {
    FakeThermal thermal;
    thermal.Add("gpu-thermal", 71000);
    thermal.Add("soc-thermal", 48500);
    thermal.Add("npu-thermal", 66000);

    REQUIRE(ReadCpuTemperatureMilliC(thermal.Path()) == 48500);
}

TEST_CASE("an x86 package zone wins over an ARM-style one", "[system][temperature]") {
    FakeThermal thermal;
    thermal.Add("cpu-thermal", 50000);
    thermal.Add("x86_pkg_temp", 61000);

    REQUIRE(ReadCpuTemperatureMilliC(thermal.Path()) == 61000);
}

TEST_CASE("without a known zone the hottest readable one is reported", "[system][temperature]") {
    FakeThermal thermal;
    thermal.Add("bigcore0-thermal", 55000);
    thermal.Add("littlecore-thermal", 52000);
    thermal.Add("broken-thermal", 0, false);

    REQUIRE(ReadCpuTemperatureMilliC(thermal.Path()) == 55000);
}

TEST_CASE("no thermal zones is -1, never a throw", "[system][temperature]") {
    FakeThermal thermal;
    REQUIRE(ReadCpuTemperatureMilliC(thermal.Path()) == -1);
    REQUIRE(ReadCpuTemperatureMilliC((thermal.root / "missing").string()) == -1);
}

TEST_CASE("every readable thermal zone is listed by type, in zone order", "[system][temperature]") {
    FakeThermal thermal;
    thermal.Add("soc-thermal", 48500);
    thermal.Add("broken-thermal", 0, false);
    thermal.Add("gpu-thermal", 47000);
    thermal.Add("npu-thermal", 66000);

    const auto zones = ReadThermalZonesMilliC(thermal.Path());
    REQUIRE(zones.size() == 3);
    REQUIRE(zones[0] == std::pair<std::string, int>{"soc-thermal", 48500});
    REQUIRE(zones[1] == std::pair<std::string, int>{"gpu-thermal", 47000});
    REQUIRE(zones[2] == std::pair<std::string, int>{"npu-thermal", 66000});
    REQUIRE(ReadThermalZonesMilliC((thermal.root / "missing").string()).empty());
}

TEST_CASE("a clock is throttled when a CPU or GPU cooling device is engaged, not when only a fan runs", "[system][temperature]") {
    // one fake directory at a time: every FakeThermal uses the same path
    {
        FakeThermal fanOnly;
        fanOnly.AddCooling("pwm-fan", 3);
        fanOnly.AddCooling("cpufreq-cpu0", 0);
        fanOnly.AddCooling("cpufreq-cpu4", 0);
        fanOnly.AddCooling("devfreq-fb000000.gpu", 0);
        REQUIRE_FALSE(ReadClockThrottled(fanOnly.Path()));
    }
    {
        FakeThermal cpuHeld;
        cpuHeld.AddCooling("pwm-fan", 0);
        cpuHeld.AddCooling("cpufreq-cpu4", 2);
        REQUIRE(ReadClockThrottled(cpuHeld.Path()));
    }
    {
        FakeThermal gpuHeld;
        gpuHeld.AddCooling("devfreq-fb000000.gpu", 1);
        REQUIRE(ReadClockThrottled(gpuHeld.Path()));
    }
}

#include "CameraMode.h"

TEST_CASE("a camera running its own default mode is not reported as substituted", "[camera][mode]") {
    CameraMode actual;
    actual.width = 640;
    actual.height = 480;
    actual.pixelFormat = FrameFormat::MJPEG;

    REQUIRE(ModeHonoursRequest(std::nullopt, actual));

    CameraMode same = actual;
    same.fps = 15.0; // only the size and the format decide whether a request was honoured
    REQUIRE(ModeHonoursRequest(same, actual));

    CameraMode other = actual;
    other.width = 320;
    other.height = 240;
    REQUIRE_FALSE(ModeHonoursRequest(other, actual));

    CameraMode otherFormat = actual;
    otherFormat.pixelFormat = FrameFormat::YUYV;
    REQUIRE_FALSE(ModeHonoursRequest(otherFormat, actual));
}
