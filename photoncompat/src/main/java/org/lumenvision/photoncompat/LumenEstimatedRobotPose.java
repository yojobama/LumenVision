package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose3d;

import java.util.Collections;
import java.util.List;

/** A robot pose estimated from one result by {@link LumenPoseEstimator}; shaped like photonlib's EstimatedRobotPose. */
public class LumenEstimatedRobotPose {
    private final Pose3d estimatedPose;
    private final double timestampSeconds;
    private final List<LumenTrackedTarget> targetsUsed;
    private final LumenPoseStrategy strategy;

    LumenEstimatedRobotPose(Pose3d estimatedPose, double timestampSeconds, List<LumenTrackedTarget> targetsUsed,
            LumenPoseStrategy strategy) {
        this.estimatedPose = estimatedPose;
        this.timestampSeconds = timestampSeconds;
        this.targetsUsed = Collections.unmodifiableList(targetsUsed);
        this.strategy = strategy;
    }

    /** The robot's field-relative pose. */
    public Pose3d getEstimatedPose() {
        return estimatedPose;
    }

    /** The frame's capture time, same clock as {@link LumenPipelineResult#getTimestampSeconds()}; pass to {@code addVisionMeasurement}. */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /** The targets that contributed to the estimate. */
    public List<LumenTrackedTarget> getTargetsUsed() {
        return targetsUsed;
    }

    /** The strategy that produced this estimate (a fallback, if the primary one had no data). */
    public LumenPoseStrategy getStrategy() {
        return strategy;
    }
}
