package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.StringPublisher;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.lumenvision.lib.LumenDetector;

/** PhotonCamera's pipelines and driver mode against the coprocessor's graph, on one in-process NT instance. */
@Tag("edge")
class EdgePhotonCameraTest {
    private static final String ROOT = "lumenvision";
    private NetworkTableInstance nt;
    private StringPublisher graphPub;

    @BeforeEach
    void start() {
        nt = NetworkTableInstance.create();
        LumenDetector.setVersionCheckEnabled(false);
        graphPub = nt.getStringTopic("/" + ROOT + "/.graph").publish();
    }

    @AfterEach
    void stop() {
        graphPub.close();
        nt.close();
    }

    // camera "front" feeding two detectors, as Server/NetworkTablesControlService.BuildGraphJson publishes it
    private static String graph(boolean tagsEnabled, boolean piecesEnabled) {
        return "{\"nodes\":[{\"id\":1,\"name\":\"front\",\"table\":\"front\",\"kind\":\"Camera\",\"enabled\":true,\"recording\":false,\"input\":null},"
                + "{\"id\":2,\"name\":\"tags\",\"table\":\"tags\",\"kind\":\"ApriltagSink\",\"enabled\":" + tagsEnabled + ",\"recording\":false,\"input\":1},"
                + "{\"id\":3,\"name\":\"pieces\",\"table\":\"pieces\",\"kind\":\"ObjectDetectionSink\",\"enabled\":" + piecesEnabled
                + ",\"recording\":false,\"input\":1}]}";
    }

    // The camera's running pipeline is 1 (set up in the UI, or by setPipelineIndex in an earlier robot program run). A driver toggles
    // driver mode on and, once the coprocessor has stopped the detectors (the graph reports both disabled), off again: photonlib returns
    // to the pipeline that was running.
    @Test
    void G5_leavingDriverModeReturnsToThePipelineThatWasRunning() {
        try (PhotonCamera camera = new PhotonCamera(nt, ROOT, "front");
                BooleanSubscriber tagsEnabled = nt.getBooleanTopic("/" + ROOT + "/tags/config/enabled").subscribe(false);
                BooleanSubscriber piecesEnabled = nt.getBooleanTopic("/" + ROOT + "/pieces/config/enabled").subscribe(false)) {
            graphPub.set(graph(false, true));
            assertEquals(1, camera.getPipelineIndex());

            camera.setDriverMode(true);
            assertFalse(tagsEnabled.get());
            assertFalse(piecesEnabled.get());
            graphPub.set(graph(false, false)); // the coprocessor applied it

            camera.setDriverMode(false);
            assertTrue(piecesEnabled.get(), "pipeline 1 (pieces) was running before driver mode and must run again");
            assertFalse(tagsEnabled.get(), "pipeline 0 (tags) was stopped before driver mode and must stay stopped");
            graphPub.set(graph(tagsEnabled.get(), piecesEnabled.get()));
            assertEquals(1, camera.getPipelineIndex());
        }
    }
}
