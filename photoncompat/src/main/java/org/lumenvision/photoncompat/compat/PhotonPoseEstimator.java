package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.geometry.Transform3d;
import org.lumenvision.photoncompat.LumenEstimatedRobotPose;
import org.lumenvision.photoncompat.LumenPoseEstimator;

import java.util.Optional;

/**
 * Thin {@link LumenPoseEstimator} wrapper for drop-in migration. {@code update()} returns {@link
 * LumenEstimatedRobotPose} directly, since there is no {@code PoseStrategy} to hide.
 */
public class PhotonPoseEstimator {
    private final LumenPoseEstimator lumenEstimator;

    public PhotonPoseEstimator(PhotonCamera camera, Transform3d robotToCamera) {
        this.lumenEstimator = new LumenPoseEstimator(camera.getLumenCamera(), robotToCamera);
    }

    public Optional<LumenEstimatedRobotPose> update() {
        return lumenEstimator.update();
    }

    public Optional<LumenEstimatedRobotPose> update(PhotonPipelineResult result) {
        return lumenEstimator.update(result.getLumenPipelineResult());
    }
}
