package org.lumenvision.lib;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.NetworkTablesJNI;
import edu.wpi.first.networktables.PubSubOption;
import edu.wpi.first.networktables.RawPublisher;
import edu.wpi.first.networktables.StringPublisher;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.function.BooleanSupplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;

/**
 * Edge cases of the robot-side detector against topics published the way the real coprocessor (LumenCore/NetworkTablesSink.cpp,
 * Server/NetworkTablesControlService.cs) publishes them. As on a robot, the robot program's instance is the NT server and the coprocessor
 * is a client.
 */
@Tag("edge")
class EdgeLumenDetectorNtTest {
    private static final int NT3_PORT = 18829;
    private static final int NT4_PORT = 18830;
    private static final String ROOT = "lumenvision";

    private NetworkTableInstance robot;
    private NetworkTableInstance coprocessor;

    @BeforeEach
    void start() {
        robot = NetworkTableInstance.create();
        robot.startServer("", "127.0.0.1", NT3_PORT, NT4_PORT);
        coprocessor = NetworkTableInstance.create();
        coprocessor.setServer("127.0.0.1", NT4_PORT);
        coprocessor.startClient4("coprocessor");
        assertTrue(waitFor(() -> coprocessor.isConnected() && coprocessor.getServerTimeOffset().isPresent()));
    }

    @AfterEach
    void stop() {
        LumenDetector.setVersionCheckEnabled(true);
        coprocessor.stopClient();
        robot.stopServer();
        coprocessor.close();
        robot.close();
    }

    private static boolean waitFor(BooleanSupplier condition) {
        long deadline = System.currentTimeMillis() + 3000;
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

    private RawPublisher resultPublisher(String table) {
        return coprocessor.getRawTopic("/" + ROOT + "/" + table + "/result").publish("raw",
                PubSubOption.sendAll(true), PubSubOption.keepDuplicates(true));
    }

    // .version and .protocol are separate topics; when the robot reads a result after .version has arrived but before .protocol has
    // (a coprocessor whose .protocol announce lands in a later batch), the protocol is taken as "none published" and every later
    // result is ignored for the life of the robot program, although the coprocessor speaks the right protocol a moment later.
    @Test
    void G1_aProtocolArrivingJustAfterTheVersionIsNotRefusedForever() throws IOException {
        LumenDetector.setVersionCheckEnabled(true);
        try (LumenAprilTagDetector front = new LumenAprilTagDetector(robot, ROOT, "front");
                StringPublisher versionPub = coprocessor.getStringTopic("/" + ROOT + "/.version").publish();
                IntegerPublisher protocolPub = coprocessor.getIntegerTopic("/" + ROOT + "/.protocol").publish();
                RawPublisher pub = resultPublisher("front")) {
            versionPub.set(LumenDetector.getLibraryVersion());
            assertTrue(waitFor(() -> !robot.getEntry("/" + ROOT + "/.version").getString("").isEmpty()));
            front.getLatestResult(); // one robot loop runs between the two topics arriving

            protocolPub.set(LumenCoprocessor.PROTOCOL_VERSION);
            pub.set(golden(), NetworkTablesJNI.now());
            assertTrue(waitFor(() -> robot.getEntry("/" + ROOT + "/front/result").getRaw(new byte[0]).length > 0
                    && robot.getEntry("/" + ROOT + "/.protocol").getInteger(-1) == LumenCoprocessor.PROTOCOL_VERSION));
            assertTrue(waitFor(() -> front.getLatestResult().getSequenceId() == 42),
                    "results stay refused although the coprocessor now reports protocol " + LumenCoprocessor.PROTOCOL_VERSION);
        }
    }

    // The warning says results "are ignored until both are updated together"; the coprocessor is then updated (its service restarts and
    // reconnects speaking the right protocol) while the robot program keeps running, as at an event. Results must resume.
    @Test
    void G1_resultsResumeOnceTheCoprocessorIsUpdatedToTheRightProtocol() throws IOException {
        LumenDetector.setVersionCheckEnabled(true);
        try (LumenAprilTagDetector front = new LumenAprilTagDetector(robot, ROOT, "front")) {
            try (StringPublisher versionPub = coprocessor.getStringTopic("/" + ROOT + "/.version").publish();
                    IntegerPublisher protocolPub = coprocessor.getIntegerTopic("/" + ROOT + "/.protocol").publish()) {
                versionPub.set("0.0.1-old");
                protocolPub.set(LumenCoprocessor.PROTOCOL_VERSION - 1);
                assertTrue(waitFor(() -> robot.getEntry("/" + ROOT + "/.protocol").getInteger(-1) == LumenCoprocessor.PROTOCOL_VERSION - 1
                        && !robot.getEntry("/" + ROOT + "/.version").getString("").isEmpty()));
                assertEquals(0, front.getLatestResult().getSequenceId()); // refused, and warned
            }
            // the old coprocessor service goes away; the updated one connects
            coprocessor.stopClient();
            coprocessor.close();
            coprocessor = NetworkTableInstance.create();
            coprocessor.setServer("127.0.0.1", NT4_PORT);
            coprocessor.startClient4("coprocessor");
            assertTrue(waitFor(() -> coprocessor.isConnected() && coprocessor.getServerTimeOffset().isPresent()));
            try (StringPublisher versionPub = coprocessor.getStringTopic("/" + ROOT + "/.version").publish();
                    IntegerPublisher protocolPub = coprocessor.getIntegerTopic("/" + ROOT + "/.protocol").publish();
                    RawPublisher pub = resultPublisher("front")) {
                versionPub.set(LumenDetector.getLibraryVersion());
                protocolPub.set(LumenCoprocessor.PROTOCOL_VERSION);
                pub.set(golden(), NetworkTablesJNI.now());
                assertTrue(waitFor(() -> robot.getEntry("/" + ROOT + "/front/result").getRaw(new byte[0]).length > 0
                        && robot.getEntry("/" + ROOT + "/.protocol").getInteger(-1) == LumenCoprocessor.PROTOCOL_VERSION));
                assertTrue(waitFor(() -> front.getLatestResult().getSequenceId() == 42),
                        "results stay refused after the coprocessor was updated to protocol " + LumenCoprocessor.PROTOCOL_VERSION);
            }
        }
    }

    // The coprocessor's graph as Server/NetworkTablesControlService.BuildGraphJson publishes it when the UI's camera and its AprilTag
    // detector are both named "front" (ComputeNodeTables makes the detector's table unique: "front-2"). The example robot opens the
    // detector by its UI name, as LumenAprilTagDetector's javadoc says; and PhotonCameraSim names a camera's detector exactly so.
    @Test
    void G2_aDetectorNamedLikeItsCameraIsReadByItsUiName() throws IOException {
        LumenDetector.setVersionCheckEnabled(false);
        String graph = "{\"nodes\":[{\"id\":1,\"name\":\"front\",\"table\":\"front\",\"kind\":\"Camera\",\"enabled\":true,\"recording\":false,\"input\":null},"
                + "{\"id\":2,\"name\":\"front\",\"table\":\"front-2\",\"kind\":\"ApriltagSink\",\"enabled\":true,\"recording\":false,\"input\":1}]}";
        try (LumenAprilTagDetector front = new LumenAprilTagDetector(robot, ROOT, "front");
                StringPublisher graphPub = coprocessor.getStringTopic("/" + ROOT + "/.graph").publish();
                RawPublisher pub = resultPublisher("front-2")) {
            graphPub.set(graph);
            pub.set(golden(), NetworkTablesJNI.now());
            assertTrue(waitFor(() -> robot.getEntry("/" + ROOT + "/front-2/result").getRaw(new byte[0]).length > 0
                    && !robot.getEntry("/" + ROOT + "/.graph").getString("").isEmpty()));
            assertTrue(waitFor(() -> front.getLatestResult().getSequenceId() == 42),
                    "the UI's detector 'front' publishes under front-2; LumenAprilTagDetector(\"front\") read sequence "
                            + front.getLatestResult().getSequenceId() + ", kind " + front.getNodeKind());
        }
    }
}
