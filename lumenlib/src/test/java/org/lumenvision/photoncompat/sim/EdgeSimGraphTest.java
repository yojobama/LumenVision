package org.lumenvision.photoncompat.sim;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.networktables.NetworkTableInstance;
import java.util.List;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.lumenvision.lib.LumenAprilTagDetector;
import org.lumenvision.lib.LumenCoprocessor;
import org.lumenvision.lib.LumenDetector;
import org.lumenvision.lib.LumenGraph;

/** The simulated cameras' shared .graph topic with camera names that need escaping in JSON. */
@Tag("edge")
class EdgeSimGraphTest {
    private static final String ROOT = "lumenvision";
    private NetworkTableInstance nt;

    @BeforeEach
    void start() {
        nt = NetworkTableInstance.create();
        LumenDetector.setVersionCheckEnabled(false);
    }

    @AfterEach
    void stop() {
        nt.close();
    }

    // a name the LumenVision UI accepts (and NetworkTablesSink sanitises to a table) but that holds a JSON metacharacter
    @Test
    void G7_aCameraNameWithAQuoteOrBackslashKeepsTheGraphReadable() {
        try (LumenAprilTagDetector front = new LumenAprilTagDetector(nt, ROOT, "front");
                LumenAprilTagDetector odd = new LumenAprilTagDetector(nt, ROOT, "Bob's \"back\" cam\\2");
                PhotonCameraSim frontSim = new PhotonCameraSim(front, new SimCameraProperties());
                PhotonCameraSim oddSim = new PhotonCameraSim(odd, new SimCameraProperties())) {
            frontSim.update(new Pose3d(), List.of());
            oddSim.update(new Pose3d(), List.of());

            LumenGraph graph = new LumenCoprocessor(nt, ROOT).getGraph();
            assertEquals(4, graph.getNodes().size(), "two simulated cameras, each with its detector; graph JSON was: "
                    + nt.getEntry("/" + ROOT + "/.graph").getString(""));
            assertTrue(graph.findCamera("Bob's \"back\" cam\\2").isPresent());
            front.getLatestResult();
            assertEquals(LumenGraph.APRILTAG_DETECTOR, front.getNodeKind().orElse("none"),
                    "the other camera's name must not hide this one's node");
        }
    }
}
