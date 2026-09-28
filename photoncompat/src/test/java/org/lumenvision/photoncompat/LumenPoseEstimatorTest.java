package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import java.util.ArrayList;
import java.util.Optional;
import org.junit.jupiter.api.Test;

// Tests LumenPoseEstimator.compose directly (package-private) because the public path needs a
// native-backed NetworkTableInstance, which this module's tests cannot load
class LumenPoseEstimatorTest {

    @Test
    void compose_appliesFieldToCameraAndCameraToRobotOffset() {
        double[] identityRowMajor = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        // camera at field (5, 0, 0) with no rotation, from a 3-tag multi-tag solve
        LumenMultiTagResult multiTag = new LumenMultiTagResult(5.0, 0.0, 0.0, identityRowMajor, 3, 0.4);
        LumenPipelineResult result =
                new LumenPipelineResult(new ArrayList<>(), 12.0, Optional.of(multiTag));

        // camera mounted 1m forward of the robot origin, so the robot sits 1m behind the camera
        Transform3d cameraToRobot = new Transform3d(new Translation3d(1, 0, 0), new Rotation3d()).inverse();

        Optional<LumenEstimatedRobotPose> estimated = LumenPoseEstimator.compose(result, cameraToRobot);

        assertTrue(estimated.isPresent());
        Pose3d robotPose = estimated.get().getEstimatedPose();
        assertEquals(4.0, robotPose.getX(), 1e-9);
        assertEquals(0.0, robotPose.getY(), 1e-9);
        assertEquals(0.0, robotPose.getZ(), 1e-9);
        assertEquals(12.0, estimated.get().getTimestampSeconds(), 1e-9);
        assertEquals(3, estimated.get().getTagCount());
        assertEquals(0.4, estimated.get().getReprojectionErrorPixels(), 1e-9);
    }

    @Test
    void compose_emptyWhenNoMultiTagResultThisFrame() {
        LumenPipelineResult result = new LumenPipelineResult(new ArrayList<>(), 12.0, Optional.empty());

        assertTrue(LumenPoseEstimator.compose(result, new Transform3d()).isEmpty());
    }
}
