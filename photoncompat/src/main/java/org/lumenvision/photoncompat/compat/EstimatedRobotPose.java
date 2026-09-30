package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.geometry.Pose3d;
import org.lumenvision.photoncompat.LumenEstimatedRobotPose;

import java.util.List;
import java.util.stream.Collectors;

/** A robot pose estimate, as photonlib's EstimatedRobotPose (public final fields). */
public class EstimatedRobotPose {
    public final Pose3d estimatedPose;
    public final double timestampSeconds;
    public final List<PhotonTrackedTarget> targetsUsed;
    public final PoseStrategy strategy;

    public EstimatedRobotPose(Pose3d estimatedPose, double timestampSeconds, List<PhotonTrackedTarget> targetsUsed, PoseStrategy strategy) {
        this.estimatedPose = estimatedPose;
        this.timestampSeconds = timestampSeconds;
        this.targetsUsed = targetsUsed;
        this.strategy = strategy;
    }

    static EstimatedRobotPose from(LumenEstimatedRobotPose pose) {
        return new EstimatedRobotPose(pose.getEstimatedPose(), pose.getTimestampSeconds(),
                pose.getTargetsUsed().stream().map(PhotonTrackedTarget::new).collect(Collectors.toList()),
                PoseStrategy.fromLumen(pose.getStrategy()));
    }
}
