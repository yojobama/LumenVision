package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose3d;

import java.util.List;

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
}
