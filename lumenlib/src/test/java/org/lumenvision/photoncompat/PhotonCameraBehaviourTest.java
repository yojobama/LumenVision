package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.StringPublisher;
import java.util.function.BooleanSupplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.lumenvision.lib.LumenCoprocessor;

// What a robot observes through PhotonCamera, against a fake coprocessor that applies each request and republishes its graph.
class PhotonCameraBehaviourTest {
    private static final int NT3_PORT = 18829;
    private static final int NT4_PORT = 18830;
    private static final String ROOT = "lumenvision";
    private static final int CAMERA_ID = 41;
    private static final int TAGS_ID = 42;
    private static final int NOTES_ID = 43;

    private NetworkTableInstance server;
    private NetworkTableInstance coprocessor;
    private NetworkTableInstance robot;
    private PhotonCamera camera;
    private FakeCoprocessor fake;

    // a camera "front" with two detectors: "tags" (table "front", the camera's name) and "notes" (table "43"); tags runs first
    private static final class FakeCoprocessor implements AutoCloseable {
        final BooleanSubscriber tagsRequest;
        final BooleanSubscriber notesRequest;
        final IntegerSubscriber tagsSnapshots;
        final IntegerSubscriber notesSnapshots;
        final IntegerSubscriber cameraSnapshots;
        final StringPublisher graph;
        boolean tagsOn = true;
        boolean notesOn = false;

        FakeCoprocessor(NetworkTableInstance nt) {
            tagsRequest = nt.getBooleanTopic("/" + ROOT + "/front/config/enabled").subscribe(true);
            notesRequest = nt.getBooleanTopic("/" + ROOT + "/" + NOTES_ID + "/config/enabled").subscribe(false);
            tagsSnapshots = nt.getIntegerTopic("/" + ROOT + "/front/config/snapshot").subscribe(0);
            notesSnapshots = nt.getIntegerTopic("/" + ROOT + "/" + NOTES_ID + "/config/snapshot").subscribe(0);
            cameraSnapshots = nt.getIntegerTopic("/" + ROOT + "/" + CAMERA_ID + "/config/snapshot").subscribe(0);
            graph = nt.getStringTopic("/" + ROOT + "/.graph").publish();
            publishGraph();
        }

        void publishGraph() {
            graph.set("{\"nodes\":[{\"id\":" + CAMERA_ID + ",\"name\":\"front\",\"table\":\"" + CAMERA_ID + "\",\"kind\":\"Camera\",\"enabled\":true,\"input\":null},"
                    + "{\"id\":" + TAGS_ID + ",\"name\":\"front - tags\",\"table\":\"front\",\"kind\":\"ApriltagSink\",\"enabled\":" + tagsOn + ",\"input\":" + CAMERA_ID + "},"
                    + "{\"id\":" + NOTES_ID + ",\"name\":\"front - notes\",\"table\":\"" + NOTES_ID + "\",\"kind\":\"ObjectDetectionSink\",\"enabled\":" + notesOn + ",\"input\":" + CAMERA_ID + "}]}");
        }

        // applies the enabled requests the robot has written, as the coprocessor's control loop would
        void apply() {
            if (tagsRequest.getTopic().exists()) tagsOn = tagsRequest.get();
            if (notesRequest.getTopic().exists()) notesOn = notesRequest.get();
            publishGraph();
        }

        @Override
        public void close() {
            tagsRequest.close();
            notesRequest.close();
            tagsSnapshots.close();
            notesSnapshots.close();
            cameraSnapshots.close();
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
        fake = new FakeCoprocessor(coprocessor);
        assertTrue(waitFor(() -> robot.isConnected() && coprocessor.isConnected()));
        assertTrue(waitFor(() -> !new LumenCoprocessor(robot, ROOT).getGraph().getNodes().isEmpty()));
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

    @Test
    void switchingThePipelineRunsThatDetectorAndStopsTheOthers() {
        assertEquals(0, camera.getPipelineIndex());
        assertEquals("front", camera.getLumenDetector().getName());

        camera.setPipelineIndex(1);
        assertTrue(waitFor(() -> fake.notesRequest.get() && !fake.tagsRequest.get()));
        fake.apply();

        assertTrue(waitFor(() -> camera.getPipelineIndex() == 1));
        assertEquals(String.valueOf(NOTES_ID), camera.getLumenDetector().getName());
        assertFalse(camera.getDriverMode());
    }

    @Test
    void driverModeStopsEveryDetectorAndTurningItOffRestoresThePipeline() {
        assertFalse(camera.getDriverMode());
        camera.setDriverMode(true);
        assertTrue(waitFor(() -> !fake.tagsRequest.get()));
        fake.apply();
        assertTrue(waitFor(() -> camera.getDriverMode()));

        camera.setDriverMode(false);
        assertTrue(waitFor(() -> fake.tagsRequest.get()));
        fake.apply();
        assertTrue(waitFor(() -> !camera.getDriverMode()));
        assertEquals(0, camera.getPipelineIndex());
    }

    @Test
    void inputSnapshotsGoToTheCameraAndOutputSnapshotsToTheRunningDetector() {
        camera.takeInputSnapshot();
        camera.takeOutputSnapshot();
        camera.takeOutputSnapshot();
        assertTrue(waitFor(() -> fake.cameraSnapshots.get() == 1 && fake.tagsSnapshots.get() == 2));
        assertEquals(0, fake.notesSnapshots.get());
    }

    @Test
    void namedPipelinesSetTheirOrder() {
        try (PhotonCamera named = new PhotonCamera(robot, ROOT, "front").withPipelines("front - notes", "front - tags")) {
            // the listed order is the pipeline order: notes is pipeline 0, and tags (the one running) is pipeline 1
            assertEquals(1, named.getPipelineIndex());
            named.setPipelineIndex(0);
            assertTrue(waitFor(() -> fake.notesRequest.get() && !fake.tagsRequest.get()));
        }
    }
}
