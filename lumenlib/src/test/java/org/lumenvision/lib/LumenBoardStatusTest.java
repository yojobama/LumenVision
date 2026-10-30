package org.lumenvision.lib;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class LumenBoardStatusTest {
    @Test
    void whatAnOlderCoprocessorDoesNotReportReadsAsUnknown() {
        // the fields a coprocessor published before the board readings were added
        LumenBoardStatus status = LumenBoardStatus.parse(
                "{\"uptimeSeconds\":5.0,\"nodeCount\":1,\"serverTimeOffsetUs\":null,\"cpuUsage\":12.0,\"cpuTemperature\":50,\"ramUsage\":0}", 7.0)
                .orElseThrow();

        assertEquals(7.0, status.getTimestampSeconds());
        assertEquals(5.0, status.getServiceUptimeSeconds());
        assertEquals(1, status.getActiveNodeCount());
        assertTrue(status.getServerTimeOffsetMicros().isEmpty());
        assertTrue(Double.isNaN(status.getCpuUsagePercent()));
        assertTrue(Double.isNaN(status.getCpuTemperatureCelsius()));
        assertTrue(Double.isNaN(status.getMaxTemperatureCelsius()));
        assertTrue(status.getTemperaturesCelsius().isEmpty());
        assertFalse(status.isThrottled());
        assertTrue(Double.isNaN(status.getRamUsagePercent()));
        assertTrue(Double.isNaN(status.getDiskFreeMegabytes()));
        assertTrue(Double.isNaN(status.getBoardUptimeSeconds()));
    }

    @Test
    void aBoardWithoutATemperatureSensorReportsNoTemperature() {
        LumenBoardStatus status = LumenBoardStatus.parse("{\"cpuTemperatureC\":null,\"temperaturesC\":{}}", 1.0).orElseThrow();

        assertTrue(Double.isNaN(status.getCpuTemperatureCelsius()));
        assertTrue(status.getTemperaturesCelsius().isEmpty());
    }

    @Test
    void textThatIsNotAJsonObjectIsNoStatus() {
        assertTrue(LumenBoardStatus.parse("", 1.0).isEmpty());
        assertTrue(LumenBoardStatus.parse(null, 1.0).isEmpty());
        assertTrue(LumenBoardStatus.parse("not json", 1.0).isEmpty());
        assertTrue(LumenBoardStatus.parse("[1, 2]", 1.0).isEmpty());
    }
}
