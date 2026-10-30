package org.lumenvision.lib;

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
        LumenDetector.setVersionCheckEnabled(false);
        LumenAprilTagDetector camera = new LumenAprilTagDetector(server, "lumenvision", "front");
        LumenCoprocessor coprocessor = new LumenCoprocessor(server, "lumenvision");
        try {
            long deadline = System.currentTimeMillis() + 40_000;
            LumenAprilTagResult result = null;
            while (System.currentTimeMillis() < deadline) {
                LumenAprilTagResult latest = camera.getLatestResult();
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
                LumenAprilTagResult any = camera.getLatestResult();
                System.out.println("HIL: latest sequence " + any.getSequenceId() + ", targets " + any.getTargets().size());
            }
            assertTrue(result != null, "no result with a target arrived from the coprocessor");

            LumenAprilTagTarget target = result.getBestTarget().orElseThrow();
            double nowSeconds = server.getServerTimeOffset().isPresent() ? edu.wpi.first.networktables.NetworkTablesJNI.now() / 1e6 : 0;
            System.out.printf("HIL: sequence %d, %d targets, best id %d, area %.3f%%, yaw %.2f, pitch %.2f, latency %.1f ms, capture age %.3f s%n",
                    result.getSequenceId(), result.getTargets().size(), target.getFiducialId(), target.getArea(), target.getYaw(), target.getPitch(),
                    result.getLatencyMillis(), nowSeconds - result.getTimestampSeconds());
            assertTrue(target.getFiducialId() >= 0);
            assertTrue(result.getLatencyMillis() >= 0);
            assertTrue(Math.abs(nowSeconds - result.getTimestampSeconds()) < 2.0, "the capture time should be recent on this machine's clock");

            // control round trips: write a setting, then read it back from the coprocessor's status topic
            camera.setFpsLimit(7);
            long until = System.currentTimeMillis() + 10_000;
            while (System.currentTimeMillis() < until && camera.getFpsLimit() != 7) Thread.sleep(100);
            System.out.println("HIL: fps limit read back " + camera.getFpsLimit() + ", connected " + camera.isConnected());
            assertTrue(camera.getFpsLimit() == 7, "the FPS limit should be reported back in status/fpsLimit");

            camera.takeSnapshot();
            coprocessor.setLed(LumenLedMode.BLINK);
            Thread.sleep(3000);
            System.out.println("HIL: led reads back " + coprocessor.getLed());
            camera.setFpsLimit(0);
            coprocessor.setLed(LumenLedMode.AUTO);
            Thread.sleep(1000);
        } finally {
            camera.close();
            server.stopServer();
            server.close();
        }
    }

    // A detector stopped over NT stops publishing and one started again resumes; the graph and protocol come from the coprocessor.
    @Test
    void aDetectorCanBeStoppedAndStartedOverNetworkTables() throws Exception {
        assumeTrue(System.getProperty("lumen.hil") != null, "hardware test: set -Dlumen.hil=true");

        NetworkTableInstance server = NetworkTableInstance.create();
        server.startServer("", "0.0.0.0", 1735, 5810);
        LumenDetector.setVersionCheckEnabled(false);
        LumenCoprocessor coprocessor = new LumenCoprocessor(server, "lumenvision");
        LumenDetector<?> detector = null;
        try {
            long deadline = System.currentTimeMillis() + 40_000;
            while (System.currentTimeMillis() < deadline && coprocessor.getGraph().getNodes().isEmpty()) Thread.sleep(200);
            LumenGraph graph = coprocessor.getGraph();
            assertTrue(!graph.getNodes().isEmpty(), "the coprocessor should publish .graph");
            assertTrue(coprocessor.getProtocol() == LumenCoprocessor.PROTOCOL_VERSION, "protocol " + coprocessor.getProtocol());

            String wanted = System.getProperty("lumen.hil.node");
            LumenGraph.Node node = graph.getNodes().stream()
                    .filter(n -> n.isDetector() && (wanted == null ? n.isEnabled() : n.getTable().equals(wanted)))
                    .findFirst().orElseThrow();
            System.out.println("HIL: graph has " + graph.getNodes().size() + " nodes; using detector " + node.getName() + " at table " + node.getTable());
            detector = node.isObjectDetector() ? new LumenObjectDetector(server, "lumenvision", node.getTable())
                    : new LumenAprilTagDetector(server, "lumenvision", node.getTable());

            long first = awaitAdvance(detector, 0, 15_000);
            assertTrue(first > 0, "the detector should publish results");

            detector.setEnabled(false);
            long until = System.currentTimeMillis() + 10_000;
            while (System.currentTimeMillis() < until && detector.isEnabled()) Thread.sleep(100);
            assertTrue(!detector.isEnabled(), "status/enabled should read false");
            Thread.sleep(1500);
            long stopped = detector.getLatestResult().getSequenceId();
            Thread.sleep(1500);
            assertTrue(detector.getLatestResult().getSequenceId() == stopped, "a stopped detector should publish no more results");
            System.out.println("HIL: stopped at sequence " + stopped);

            detector.setEnabled(true);
            long resumed = awaitAdvance(detector, stopped, 15_000);
            assertTrue(resumed > stopped, "a started detector should publish again");
            until = System.currentTimeMillis() + 10_000;
            while (System.currentTimeMillis() < until && !detector.isEnabled()) Thread.sleep(100);
            assertTrue(detector.isEnabled());
            System.out.println("HIL: resumed at sequence " + resumed);
        } finally {
            if (detector != null) detector.setEnabled(true);
            Thread.sleep(500);
            if (detector != null) detector.close();
            server.stopServer();
            server.close();
        }
    }

    private static long awaitAdvance(LumenDetector<?> detector, long after, long timeoutMillis) throws InterruptedException {
        long deadline = System.currentTimeMillis() + timeoutMillis;
        while (System.currentTimeMillis() < deadline) {
            long sequence = detector.getLatestResult().getSequenceId();
            if (sequence > after) return sequence;
            Thread.sleep(100);
        }
        return after;
    }

    // Recording one node over NT: the coprocessor starts a RecordSink on it and reports it back, and stops it again.
    @Test
    void aNodeCanBeRecordedAndStoppedOverNetworkTables() throws Exception {
        assumeTrue(System.getProperty("lumen.hil") != null, "hardware test: set -Dlumen.hil=true");

        NetworkTableInstance server = NetworkTableInstance.create();
        server.startServer("", "0.0.0.0", 1735, 5810);
        LumenCoprocessor coprocessor = new LumenCoprocessor(server, "lumenvision");
        LumenNode node = null;
        try {
            long deadline = System.currentTimeMillis() + 40_000;
            while (System.currentTimeMillis() < deadline && coprocessor.getGraph().getNodes().isEmpty()) Thread.sleep(200);
            String table = System.getProperty("lumen.hil.node");
            LumenGraph.Node target = coprocessor.getGraph().getNodes().stream()
                    .filter(n -> table == null ? n.isCamera() : n.getTable().equals(table)).findFirst().orElseThrow();
            node = new LumenNode(server, "lumenvision", target.getTable());
            System.out.println("HIL: recording node " + target.getName() + " at table " + target.getTable());

            node.startRecording();
            long until = System.currentTimeMillis() + 15_000;
            while (System.currentTimeMillis() < until && !node.isRecording()) Thread.sleep(100);
            assertTrue(node.isRecording(), "status/recording should read true");
            Thread.sleep(3000);

            node.stopRecording();
            until = System.currentTimeMillis() + 15_000;
            while (System.currentTimeMillis() < until && node.isRecording()) Thread.sleep(100);
            assertTrue(!node.isRecording(), "status/recording should read false");
            System.out.println("HIL: recording started and stopped over NT");
        } finally {
            if (node != null) {
                node.stopRecording();
                Thread.sleep(500);
                node.close();
            }
            server.stopServer();
            server.close();
        }
    }
}
