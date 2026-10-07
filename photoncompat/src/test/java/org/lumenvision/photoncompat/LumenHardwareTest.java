package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import edu.wpi.first.networktables.NetworkTableInstance;
import java.util.Optional;
import org.junit.jupiter.api.Test;

// Runs against a real LumenVision coprocessor, not a simulation: it starts an NT4 server on port 5810 and waits for the coprocessor (set to connect to
// this machine in its Settings) to publish camera "front". Skipped unless the JVM property lumen.hil is set: ./gradlew test -Dlumen.hil=true --tests '*Hardware*'
class LumenHardwareTest {
    @Test
    void aRealCoprocessorPublishesResultsThisLibraryDecodes() throws Exception {
        assumeTrue(System.getProperty("lumen.hil") != null, "hardware test: set -Dlumen.hil=true");

        NetworkTableInstance server = NetworkTableInstance.create();
        server.startServer("", "0.0.0.0", 1735, 5810);
        LumenCamera.setVersionCheckEnabled(false);
        LumenCamera camera = new LumenCamera(server, "lumenvision", "front");
        try {
            long deadline = System.currentTimeMillis() + 40_000;
            LumenPipelineResult result = null;
            while (System.currentTimeMillis() < deadline) {
                LumenPipelineResult latest = camera.getLatestResult();
                if (latest.getSequenceId() > 0 && latest.hasTargets()) {
                    result = latest;
                    break;
                }
                Thread.sleep(200);
            }
            if (result == null) {
                StringBuilder topics = new StringBuilder();
                for (edu.wpi.first.networktables.TopicInfo info : server.getTopicInfo()) topics.append(info.name).append(' ');
                System.out.println("HIL: no result with a target; topics seen: " + topics);
                LumenPipelineResult any = camera.getLatestResult();
                System.out.println("HIL: latest sequence " + any.getSequenceId() + ", targets " + any.getTargets().size());
            }
            assertTrue(result != null, "no result with a target arrived from the coprocessor");

            LumenTrackedTarget target = result.getBestTarget().orElseThrow();
            double nowSeconds = server.getServerTimeOffset().isPresent() ? edu.wpi.first.networktables.NetworkTablesJNI.now() / 1e6 : 0;
            System.out.printf("HIL: sequence %d, %d targets, best id %d, area %.3f%%, yaw %.2f, pitch %.2f, latency %.1f ms, capture age %.3f s%n",
                    result.getSequenceId(), result.getTargets().size(), target.getFiducialId(), target.getArea(), target.getYaw(), target.getPitch(),
                    result.getLatencyMillis(), nowSeconds - result.getTimestampSeconds());
            assertTrue(target.getFiducialId() >= 0);
            assertTrue(result.getLatencyMillis() >= 0);
            assertTrue(Math.abs(nowSeconds - result.getTimestampSeconds()) < 2.0, "the capture time should be recent on this machine's clock");

            // control round trips: write a setting, then read it back from the coprocessor's status topic
            camera.setFPSLimit(7);
            camera.setDriverMode(false);
            long until = System.currentTimeMillis() + 10_000;
            while (System.currentTimeMillis() < until && camera.getFPSLimit() != 7) Thread.sleep(100);
            System.out.println("HIL: fps limit read back " + camera.getFPSLimit() + ", pipeline " + camera.getPipelineIndex() + ", connected " + camera.isConnected());
            assertTrue(camera.getFPSLimit() == 7, "the FPS limit should be reported back in status/fpsLimit");

            camera.takeInputSnapshot();
            camera.takeOutputSnapshot();
            camera.setLED(LumenLedMode.BLINK);
            Thread.sleep(3000);
            System.out.println("HIL: led reads back " + camera.getLED());
            camera.setFPSLimit(0);
            camera.setLED(LumenLedMode.DEFAULT);
            Thread.sleep(1000);
        } finally {
            camera.close();
            server.stopServer();
            server.close();
        }
    }
}
