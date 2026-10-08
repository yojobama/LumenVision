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
