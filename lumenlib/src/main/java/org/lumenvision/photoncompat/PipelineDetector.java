package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.networktables.NetworkTableInstance;
import org.lumenvision.lib.LumenDetector;
import org.lumenvision.lib.LumenPipelineResult;

/**
 * A detector node of either kind read as PhotonVision reads a pipeline: every target with both its AprilTag and its object-detection
 * fields (see {@link org.lumenvision.lib.LumenTrackedTarget}), as a PhotonCamera may be switched between the two.
 */
final class PipelineDetector extends LumenDetector<LumenPipelineResult> {
    PipelineDetector(NetworkTableInstance instance, String rootTable, String name) {
        super(instance, rootTable, name, null);
    }

    @Override
    protected LumenPipelineResult toResult(LumenPipelineResult packet) {
        return packet;
    }

    void setConstrainedSeed(Pose2d robotPoseInLayoutFrame, Transform3d robotToCamera) {
        publishConstrainedSeed(robotPoseInLayoutFrame, robotToCamera);
    }
}
