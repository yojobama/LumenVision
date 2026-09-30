package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.numbers.N1;
import edu.wpi.first.math.numbers.N3;
import edu.wpi.first.math.numbers.N8;
import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.DoublePublisher;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.NetworkTablesJNI;
import edu.wpi.first.networktables.PubSubOption;
import edu.wpi.first.networktables.RawPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.DoubleArrayPublisher;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.function.BooleanSupplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

// A NetworkTables server, a simulated coprocessor client and the robot-side LumenCamera, all in this JVM.
class LumenCameraTest {
    private static final int NT3_PORT = 18809;
    private static final int NT4_PORT = 18810;
    private static final String ROOT = "lumenvision";

    private NetworkTableInstance server;
    private NetworkTableInstance coprocessor;
    private NetworkTableInstance robot;
    private LumenCamera camera;

    @BeforeEach
    void start() {
        server = NetworkTableInstance.create();
        server.startServer("", "127.0.0.1", NT3_PORT, NT4_PORT);
        coprocessor = NetworkTableInstance.create();
        coprocessor.setServer("127.0.0.1", NT4_PORT);
        coprocessor.startClient4("coprocessor");
        robot = NetworkTableInstance.create();
        robot.setServer("127.0.0.1", NT4_PORT);
        robot.startClient4("robot");
        LumenCamera.setVersionCheckEnabled(false);
        camera = new LumenCamera(robot, ROOT, "front");
        // timestamps are only meaningful across instances once each has synchronised its clock with the server
        assertTrue(waitFor(() -> robot.isConnected() && coprocessor.isConnected()
                && robot.getServerTimeOffset().isPresent() && coprocessor.getServerTimeOffset().isPresent()));
    }

    @AfterEach
    void stop() {
        camera.close();
        robot.stopClient();
        coprocessor.stopClient();
        server.stopServer();
        robot.close();
        coprocessor.close();
        server.close();
    }

    private static boolean waitFor(BooleanSupplier condition) {
        long deadline = System.currentTimeMillis() + 5000;
        while (System.currentTimeMillis() < deadline) {
            if (condition.getAsBoolean()) return true;
            try {
                Thread.sleep(10);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        return condition.getAsBoolean();
    }

    private static byte[] golden() throws IOException {
        return Files.readAllBytes(Paths.get("..", "tests", "data", "packet_v2.bin"));
    }

    // the golden packet with a different sequence id (bytes 2..9)
    private static byte[] withSequence(byte[] packet, long sequenceId) {
        byte[] copy = packet.clone();
        ByteBuffer.wrap(copy).putLong(2, sequenceId);
        return copy;
    }

    private RawPublisher resultPublisher(String cameraTable) {
        // like the coprocessor's publisher: every value is sent, not just the latest per send period
        return coprocessor.getRawTopic("/" + ROOT + "/" + cameraTable + "/result").publish("raw",
                PubSubOption.sendAll(true), PubSubOption.keepDuplicates(true));
    }

    @Test
    void latestResultCarriesTheFramesCaptureTimeNotItsArrivalTime() throws IOException {
        try (RawPublisher pub = resultPublisher("front")) {
            long captureMicros = NetworkTablesJNI.now() - 500_000; // captured half a second ago
            pub.set(golden(), captureMicros);

            assertTrue(waitFor(() -> camera.getLatestResult().getSequenceId() == 42));
            LumenPipelineResult result = camera.getLatestResult();

            assertEquals(2, result.getTargets().size());
            assertEquals(12.345, result.getLatencyMillis(), 1e-9);
            assertTrue(result.getMultiTagResult().isPresent());
            // the robot's clock matches the coprocessor's to within the sync error; it is well before "now", unlike an arrival stamp
            double nowSeconds = NetworkTablesJNI.now() / 1e6;
            assertEquals(captureMicros / 1e6, result.getTimestampSeconds(), 0.15);
            assertTrue(nowSeconds - result.getTimestampSeconds() > 0.3);
            assertEquals(result.getCaptureTimestampMicros() + 12345, result.getPublishTimestampMicros());
        }
    }

    @Test
    void noResultYetIsAnEmptyResult() {
        LumenPipelineResult result = camera.getLatestResult();

        assertFalse(result.hasTargets());
        assertEquals(0, result.getSequenceId());
        assertTrue(result.getBestTarget().isEmpty());
        assertTrue(camera.getAllUnreadResults().isEmpty());
    }

    @Test
    void unreadResultsArriveOnceAndInOrder() throws IOException, InterruptedException {
        byte[] packet = golden();
        try (RawPublisher pub = resultPublisher("front")) {
            // values set before the server has acknowledged the new topic are sent as one latest value; a running coprocessor is past that
            Thread.sleep(400);
            long base = NetworkTablesJNI.now();
            for (long seq = 1; seq <= 3; seq++) pub.set(withSequence(packet, seq), base + seq * 1000);
            coprocessor.flush();

            List<Long> sequences = new ArrayList<>();
            assertTrue(waitFor(() -> {
                camera.getAllUnreadResults().forEach(r -> sequences.add(r.getSequenceId()));
                return sequences.size() >= 3;
            }));
            assertEquals(List.of(1L, 2L, 3L), sequences);
            assertTrue(camera.getAllUnreadResults().isEmpty());
        }
    }

    @Test
    void anUnsupportedSchemaVersionIsIgnored() throws IOException {
        byte[] foreign = golden();
        foreign[1] = 9;
        try (RawPublisher pub = resultPublisher("front")) {
            pub.set(foreign);
            assertTrue(waitFor(() -> robot.isConnected()));
            Thread.sleep(300);

            assertEquals(0, camera.getLatestResult().getSequenceId());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    @Test
    void cameraNamesMapToSanitisedTables() throws IOException {
        assertEquals("Front_Cam__1", LumenCamera.tableName("Front Cam #1"));
        try (LumenCamera named = new LumenCamera(robot, ROOT, "Front Cam"); RawPublisher pub = resultPublisher("Front_Cam")) {
            pub.set(golden(), NetworkTablesJNI.now());

            assertTrue(waitFor(() -> named.getLatestResult().getSequenceId() == 42));
        }
    }

    @Test
    void controlsAreWrittenToTheCameraAndSnapshotsAreCounted() {
        IntegerSubscriber pipeline = coprocessor.getIntegerTopic("/" + ROOT + "/front/config/pipelineIndex").subscribe(-1);
        BooleanSubscriber driver = coprocessor.getBooleanTopic("/" + ROOT + "/front/config/driverMode").subscribe(false);
        IntegerSubscriber fps = coprocessor.getIntegerTopic("/" + ROOT + "/front/config/fpsLimit").subscribe(-99);
        IntegerSubscriber input = coprocessor.getIntegerTopic("/" + ROOT + "/front/config/inputSnapshot").subscribe(0);
        IntegerSubscriber output = coprocessor.getIntegerTopic("/" + ROOT + "/front/config/outputSnapshot").subscribe(0);
        IntegerSubscriber led = coprocessor.getIntegerTopic("/" + ROOT + "/config/ledMode").subscribe(-9);

        camera.setPipelineIndex(3);
        camera.setDriverMode(true);
        camera.setFPSLimit(20);
        camera.takeInputSnapshot();
        camera.takeOutputSnapshot();
        camera.takeOutputSnapshot();
        camera.setLED(LumenLedMode.BLINK);

        assertTrue(waitFor(() -> pipeline.get() == 3 && driver.get() && fps.get() == 20 && input.get() == 1 && output.get() == 2 && led.get() == 2));
        pipeline.close();
        driver.close();
        fps.close();
        input.close();
        output.close();
        led.close();
    }

    @Test
    void settingsAreReadBackFromTheCoprocessorsStatus() {
        try (IntegerPublisher pipeline = coprocessor.getIntegerTopic("/" + ROOT + "/front/status/pipelineIndex").publish();
                BooleanPublisher driver = coprocessor.getBooleanTopic("/" + ROOT + "/front/status/driverMode").publish();
                IntegerPublisher fps = coprocessor.getIntegerTopic("/" + ROOT + "/front/status/fpsLimit").publish();
                IntegerPublisher led = coprocessor.getIntegerTopic("/" + ROOT + "/status/ledMode").publish()) {
            assertEquals(-1, camera.getPipelineIndex());
            assertFalse(camera.getDriverMode());

            pipeline.set(2);
            driver.set(true);
            fps.set(15);
            led.set(1);

            assertTrue(waitFor(() -> camera.getPipelineIndex() == 2 && camera.getDriverMode() && camera.getFPSLimit() == 15
                    && camera.getLED() == LumenLedMode.ON));
        }
    }

    @Test
    void connectionFollowsTheHeartbeat() throws InterruptedException {
        assertFalse(camera.isConnected());
        try (DoublePublisher heartbeat = coprocessor.getDoubleTopic("/" + ROOT + "/heartbeat").publish()) {
            heartbeat.set(1);
            assertTrue(waitFor(camera::isConnected));

            Thread.sleep(1300); // no further heartbeat
            assertFalse(camera.isConnected());
        }
    }

    @Test
    void intrinsicsAndDistortionAreExposedOnceCalibrated() {
        assertTrue(camera.getCameraMatrix().isEmpty());
        assertTrue(camera.getDistCoeffs().isEmpty());
        try (DoubleArrayPublisher k = coprocessor.getDoubleArrayTopic("/" + ROOT + "/front/cameraIntrinsics").publish();
                DoubleArrayPublisher d = coprocessor.getDoubleArrayTopic("/" + ROOT + "/front/cameraDistortion").publish()) {
            k.set(new double[] {900, 0, 640, 0, 910, 400, 0, 0, 1});
            d.set(new double[] {0.1, -0.2, 0.001, 0.002, 0.03});

            assertTrue(waitFor(() -> camera.getCameraMatrix().isPresent() && camera.getDistCoeffs().isPresent()));
            Matrix<N3, N3> matrix = camera.getCameraMatrix().orElseThrow();
            assertEquals(900, matrix.get(0, 0), 1e-9);
            assertEquals(400, matrix.get(1, 2), 1e-9);
            // the distortion vector always has 8 rows, padded with zeros
            Matrix<N8, N1> dist = camera.getDistCoeffs().orElseThrow();
            assertEquals(-0.2, dist.get(1, 0), 1e-9);
            assertEquals(0.03, dist.get(4, 0), 1e-9);
            assertEquals(0.0, dist.get(7, 0), 1e-9);
        }
    }
}
