package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.StringPublisher;
import java.util.function.BooleanSupplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

// What a robot observes through PhotonCamera, against a fake coprocessor that applies each request and reports it back.
// Only the fake's topic names are tied to the wire protocol, so these assertions carry over when the protocol changes.
class PhotonCameraBehaviourTest {
    private static final int NT3_PORT = 18829;
    private static final int NT4_PORT = 18830;
    private static final String ROOT = "lumenvision";
    private static final int CAMERA_ID = 41;

    private NetworkTableInstance server;
    private NetworkTableInstance coprocessor;
    private NetworkTableInstance robot;
    private PhotonCamera camera;
    private FakeCoprocessor fake;

    // applies the camera's pipeline and enabled requests and reports them back, and publishes the graph a real coprocessor would
    private static final class FakeCoprocessor implements AutoCloseable {
        final IntegerSubscriber pipelineRequest;
        final BooleanSubscriber enabledRequest;
        final IntegerSubscriber detectorSnapshots;
        final IntegerSubscriber cameraSnapshots;
        final IntegerPublisher pipelineStatus;
        final BooleanPublisher enabledStatus;
        final StringPublisher graph;

        FakeCoprocessor(NetworkTableInstance nt, String camera) {
            String base = "/" + ROOT + "/" + camera;
            pipelineRequest = nt.getIntegerTopic(base + "/config/pipelineIndex").subscribe(-1);
            enabledRequest = nt.getBooleanTopic(base + "/config/enabled").subscribe(true);
            detectorSnapshots = nt.getIntegerTopic(base + "/config/snapshot").subscribe(0);
            cameraSnapshots = nt.getIntegerTopic("/" + ROOT + "/" + CAMERA_ID + "/config/snapshot").subscribe(0);
            pipelineStatus = nt.getIntegerTopic(base + "/status/pipelineIndex").publish();
            enabledStatus = nt.getBooleanTopic(base + "/status/enabled").publish();
            graph = nt.getStringTopic("/" + ROOT + "/.graph").publish();
            pipelineStatus.set(0);
            enabledStatus.set(true);
            graph.set("{\"nodes\":[{\"id\":" + CAMERA_ID + ",\"name\":\"" + camera + "\",\"kind\":\"Camera\",\"enabled\":true,\"input\":null},"
                    + "{\"id\":" + (CAMERA_ID + 1) + ",\"name\":\"" + camera + "\",\"kind\":\"ApriltagSink\",\"enabled\":true,\"input\":" + CAMERA_ID + "}]}");
        }

        void apply() {
            if (pipelineRequest.get() >= 0) pipelineStatus.set(pipelineRequest.get());
            enabledStatus.set(enabledRequest.get());
        }

        @Override
        public void close() {
            pipelineRequest.close();
            enabledRequest.close();
            detectorSnapshots.close();
            cameraSnapshots.close();
            pipelineStatus.close();
            enabledStatus.close();
            graph.close();
        }
    }

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
        PhotonCamera.setVersionCheckEnabled(false);
        camera = new PhotonCamera(robot, ROOT, "front");
        fake = new FakeCoprocessor(coprocessor, "front");
        assertTrue(waitFor(() -> robot.isConnected() && coprocessor.isConnected()));
    }

    @AfterEach
    void stop() {
        camera.close();
        fake.close();
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

    private boolean coprocessorGraphEmpty() {
        return new org.lumenvision.lib.LumenCoprocessor(robot, ROOT).getGraph().getNodes().isEmpty();
    }

    @Test
    void switchingThePipelineIsReadBackOnceTheCoprocessorAppliesIt() {
        assertTrue(waitFor(() -> camera.getPipelineIndex() == 0));
        camera.setPipelineIndex(2);
        assertTrue(waitFor(() -> fake.pipelineRequest.get() == 2));
        fake.apply();
        assertTrue(waitFor(() -> camera.getPipelineIndex() == 2));
    }

    @Test
    void driverModeTurnsOnAndOff() {
        assertFalse(camera.getDriverMode());
        camera.setDriverMode(true);
        assertTrue(waitFor(() -> !fake.enabledRequest.get()));
        fake.apply();
        assertTrue(waitFor(() -> camera.getDriverMode()));

        camera.setDriverMode(false);
        assertTrue(waitFor(() -> fake.enabledRequest.get()));
        fake.apply();
        assertTrue(waitFor(() -> !camera.getDriverMode()));
    }

    @Test
    void inputSnapshotsGoToTheCameraAndOutputSnapshotsToTheDetector() {
        assertTrue(waitFor(() -> !coprocessorGraphEmpty()));
        camera.takeInputSnapshot();
        camera.takeOutputSnapshot();
        camera.takeOutputSnapshot();
        assertTrue(waitFor(() -> fake.cameraSnapshots.get() == 1 && fake.detectorSnapshots.get() == 2));
        assertEquals(1, fake.cameraSnapshots.get());
    }
}
