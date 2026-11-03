package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.apriltag.AprilTag;
import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.lumenvision.lib.LumenPipelineResult;
import org.lumenvision.lib.LumenTrackedTarget;

/**
 * photonlib's PhotonPoseEstimator.update(result) (v2026.3.4, lines 493-506) returns empty for a result whose timestamp equals the one it
 * last estimated from, so the common loop {@code estimator.update(camera.getLatestResult()).ifPresent(drive::addVisionMeasurement)} adds
 * each frame once even though the robot loop (50 Hz) outpaces the camera, or the camera has stopped publishing.
 */
@Tag("edge")
class EdgePhotonPoseEstimatorTest {
    private static final Pose3d TAG1 = new Pose3d(8.0, 1.0, 1.2, new Rotation3d(0, 0, Math.PI));
    private static final AprilTagFieldLayout LAYOUT = new AprilTagFieldLayout(List.of(new AprilTag(1, TAG1)), 16.0, 8.0);
    private static final Transform3d ROBOT_TO_CAMERA = new Transform3d(new Translation3d(0.3, 0.0, 0.2), new Rotation3d(0, -0.2, 0));
    private static final Pose3d ROBOT = new Pose3d(3.0, 1.5, 0.0, new Rotation3d(0, 0, 0.3));

    private static PhotonPipelineResult frame(double timestampSeconds, long sequenceId) {
        Transform3d best = new Transform3d(ROBOT.transformBy(ROBOT_TO_CAMERA), TAG1);
        LumenTrackedTarget tag = new LumenTrackedTarget(1, -1, -1, 0, 0, 1.0, 0, 0.05, best, new Transform3d(), 0.2f, -1f,
                new double[8], new double[8], timestampSeconds, 15000);
        return new PhotonPipelineResult(new LumenPipelineResult(new ArrayList<>(List.of(tag)), timestampSeconds, sequenceId, 15000, Optional.empty()));
    }

    @Test
    void G6_theSameFrameIsEstimatedOnlyOnce() {
        for (PoseStrategy strategy : new PoseStrategy[] { PoseStrategy.LOWEST_AMBIGUITY, PoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR }) {
            PhotonPoseEstimator estimator = new PhotonPoseEstimator(LAYOUT, strategy, ROBOT_TO_CAMERA);
            PhotonPipelineResult latest = frame(12.0, 7);
            assertTrue(estimator.update(latest).isPresent(), strategy + ": first loop");
            // the next robot loop, 20 ms later: the coprocessor has not published a newer frame, so getLatestResult() is the same frame
            assertTrue(estimator.update(frame(12.0, 7)).isEmpty(),
                    strategy + ": the same frame (timestamp 12.0 s) was estimated again, so addVisionMeasurement gets it twice");
            assertTrue(estimator.update(frame(12.033, 8)).isPresent(), strategy + ": a new frame");
        }
    }
}
