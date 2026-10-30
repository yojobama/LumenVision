package org.lumenvision.lib;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalLong;

/**
 * The coprocessor board's state as it last published it under {@code <rootTable>/.status} (about five times a second): temperatures,
 * whether the kernel is holding a clock down to cool it, CPU, RAM and disk use, and uptime. Read it with
 * {@link LumenCoprocessor#getBoardStatus()}.
 *
 * <p>A value the coprocessor did not report (an older version, or a board without that sensor) reads as {@code NaN}, an empty map or
 * {@code false}.
 */
public final class LumenBoardStatus {
    private static final ObjectMapper MAPPER = new ObjectMapper();

    private final double timestampSeconds;
    private final double cpuUsagePercent;
    private final double cpuTemperatureCelsius;
    private final Map<String, Double> temperaturesCelsius;
    private final boolean throttled;
    private final double ramUsedMegabytes;
    private final double ramTotalMegabytes;
    private final double diskUsedPercent;
    private final double diskFreeMegabytes;
    private final double boardUptimeSeconds;
    private final double serviceUptimeSeconds;
    private final int activeNodeCount;
    private final OptionalLong serverTimeOffsetMicros;

    private LumenBoardStatus(double timestampSeconds, JsonNode json) {
        this.timestampSeconds = timestampSeconds;
        cpuUsagePercent = number(json, "cpuUsagePercent");
        cpuTemperatureCelsius = number(json, "cpuTemperatureC");
        Map<String, Double> zones = new LinkedHashMap<>();
        JsonNode temperatures = json.path("temperaturesC");
        for (Map.Entry<String, JsonNode> zone : temperatures.properties()) {
            if (zone.getValue().isNumber()) zones.put(zone.getKey(), zone.getValue().asDouble());
        }
        temperaturesCelsius = Collections.unmodifiableMap(zones);
        throttled = json.path("throttled").asBoolean(false);
        ramUsedMegabytes = number(json, "ramUsedMB");
        ramTotalMegabytes = number(json, "ramTotalMB");
        diskUsedPercent = number(json, "diskUsedPercent");
        diskFreeMegabytes = number(json, "diskFreeMB");
        boardUptimeSeconds = number(json, "boardUptimeSeconds");
        serviceUptimeSeconds = number(json, "uptimeSeconds");
        activeNodeCount = json.path("nodeCount").asInt(0);
        JsonNode offset = json.path("serverTimeOffsetUs");
        serverTimeOffsetMicros = offset.isNumber() ? OptionalLong.of(offset.asLong()) : OptionalLong.empty();
    }

    private static double number(JsonNode json, String key) {
        JsonNode value = json.path(key);
        return value.isNumber() ? value.asDouble() : Double.NaN;
    }

    /**
     * Parses a {@code .status} value.
     *
     * @param timestampSeconds when it was published (the topic's NT timestamp, in seconds)
     * @return empty when the text is not a JSON object
     */
    static Optional<LumenBoardStatus> parse(String json, double timestampSeconds) {
        if (json == null || json.isEmpty()) return Optional.empty();
        try {
            JsonNode node = MAPPER.readTree(json);
            return node != null && node.isObject() ? Optional.of(new LumenBoardStatus(timestampSeconds, node)) : Optional.empty();
        } catch (Exception e) {
            return Optional.empty();
        }
    }

    /**
     * When the coprocessor published this, in seconds on the NetworkTables server clock (the roboRIO's FPGA time, so comparable with
     * {@code Timer.getFPGATimestamp()}). It publishes about five times a second, so a status more than a second old means it has stopped.
     */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /** Share of all CPU cores busy over the last second, 0 to 100. */
    public double getCpuUsagePercent() {
        return cpuUsagePercent;
    }

    /** The CPU's temperature in degrees Celsius (on an RK3588 the SoC sensor); {@code NaN} when the board has none readable. */
    public double getCpuTemperatureCelsius() {
        return cpuTemperatureCelsius;
    }

    /**
     * Every temperature sensor the board has, in degrees Celsius, by the kernel's zone name (on an RK3588: soc-thermal, bigcore0-thermal,
     * bigcore1-thermal, littlecore-thermal, center-thermal, gpu-thermal, npu-thermal).
     */
    public Map<String, Double> getTemperaturesCelsius() {
        return temperaturesCelsius;
    }

    /** The hottest sensor in degrees Celsius; {@code NaN} when there is none. */
    public double getMaxTemperatureCelsius() {
        return temperaturesCelsius.values().stream().mapToDouble(Double::doubleValue).max().orElse(Double.NaN);
    }

    /**
     * Whether the kernel's thermal control is holding a CPU, GPU or NPU clock down to cool the board. While it is, detection slows and
     * latency rises; more airflow or a heatsink is the fix.
     */
    public boolean isThrottled() {
        return throttled;
    }

    /** RAM in use, in megabytes (what is not available to new allocations, so page cache does not count). */
    public double getRamUsedMegabytes() {
        return ramUsedMegabytes;
    }

    /** The board's RAM in megabytes. */
    public double getRamTotalMegabytes() {
        return ramTotalMegabytes;
    }

    /** RAM in use as a share of the total, 0 to 100; {@code NaN} when either is unknown. */
    public double getRamUsagePercent() {
        return ramTotalMegabytes > 0 ? ramUsedMegabytes / ramTotalMegabytes * 100.0 : Double.NaN;
    }

    /** Share of the root file system in use, 0 to 100. Recordings and snapshots are written to it. */
    public double getDiskUsedPercent() {
        return diskUsedPercent;
    }

    /** Space left on the root file system for recordings and snapshots, in megabytes. */
    public double getDiskFreeMegabytes() {
        return diskFreeMegabytes;
    }

    /** Seconds since the board booted: a drop means it rebooted (a brownout, for example). */
    public double getBoardUptimeSeconds() {
        return boardUptimeSeconds;
    }

    /** Seconds since the coprocessor's NetworkTables connection was set up: a drop without one in board uptime means the service restarted. */
    public double getServiceUptimeSeconds() {
        return serviceUptimeSeconds;
    }

    /** How many cameras and detectors had a result published in the last second. */
    public int getActiveNodeCount() {
        return activeNodeCount;
    }

    /**
     * The coprocessor's offset to the NetworkTables server clock in microseconds; empty until the clocks are synchronised, and until then
     * result timestamps cannot be trusted for {@code addVisionMeasurement}.
     */
    public OptionalLong getServerTimeOffsetMicros() {
        return serverTimeOffsetMicros;
    }
}
