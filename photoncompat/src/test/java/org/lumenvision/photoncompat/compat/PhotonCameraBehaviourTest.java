package org.lumenvision.photoncompat.compat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
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

    private NetworkTableInstance server;
    private NetworkTableInstance coprocessor;
    private NetworkTableInstance robot;
    private PhotonCamera camera;
    private FakeCoprocessor fake;

    // applies the camera's pipeline, driver-mode and snapshot requests and publishes them back as status
    private static final class FakeCoprocessor implements AutoCloseable {
        final IntegerSubscriber pipelineRequest;
        final BooleanSubscriber driverRequest;
        final IntegerSubscriber inputSnapshots;
        final IntegerSubscriber outputSnapshots;
        final IntegerPublisher pipelineStatus;
        final BooleanPublisher driverStatus;

        FakeCoprocessor(NetworkTableInstance nt, String camera) {
            String base = "/" + ROOT + "/" + camera;
            pipelineRequest = nt.getIntegerTopic(base + "/config/pipelineIndex").subscribe(-1);
            driverRequest = nt.getBooleanTopic(base + "/config/driverMode").subscribe(false);
            inputSnapshots = nt.getIntegerTopic(base + "/config/inputSnapshot").subscribe(0);
            outputSnapshots = nt.getIntegerTopic(base + "/config/outputSnapshot").subscribe(0);
            pipelineStatus = nt.getIntegerTopic(base + "/status/pipelineIndex").publish();
            driverStatus = nt.getBooleanTopic(base + "/status/driverMode").publish();
            pipelineStatus.set(0);
            driverStatus.set(false);
        }

        void apply() {
            if (pipelineRequest.get() >= 0) pipelineStatus.set(pipelineRequest.get());
            driverStatus.set(driverRequest.get());
        }

        @Override
        public void close() {
            pipelineRequest.close();
            driverRequest.close();
            inputSnapshots.close();
            outputSnapshots.close();
            pipelineStatus.close();
            driverStatus.close();
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
        assertTrue(waitFor(() -> fake.driverRequest.get()));
        fake.apply();
        assertTrue(waitFor(() -> camera.getDriverMode()));

        camera.setDriverMode(false);
        assertTrue(waitFor(() -> !fake.driverRequest.get()));
        fake.apply();
        assertTrue(waitFor(() -> !camera.getDriverMode()));
    }

    @Test
    void eachSnapshotRequestIsSeenOnce() {
        camera.takeInputSnapshot();
        camera.takeOutputSnapshot();
        camera.takeOutputSnapshot();
        assertTrue(waitFor(() -> fake.inputSnapshots.get() == 1 && fake.outputSnapshots.get() == 2));
        assertEquals(1, fake.inputSnapshots.get());
    }
}
