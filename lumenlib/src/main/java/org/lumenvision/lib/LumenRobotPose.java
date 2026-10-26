package org.lumenvision.lib;

import edu.wpi.first.math.geometry.Pose3d;

import java.util.Collections;
import java.util.List;

/** A robot pose estimated from one result by {@link LumenPoseEstimator}. */
public class LumenRobotPose {
    private final Pose3d pose;
    private final double timestampSeconds;
    private final List<LumenTrackedTarget> targetsUsed;

    LumenRobotPose(Pose3d pose, double timestampSeconds, List<LumenTrackedTarget> targetsUsed) {
        this.pose = pose;
        this.timestampSeconds = timestampSeconds;
        this.targetsUsed = Collections.unmodifiableList(targetsUsed);
    }

    /** The robot's field-relative pose. */
    public Pose3d getPose() {
        return pose;
    }

    /** The frame's capture time, same clock as {@link LumenPipelineResult#getTimestampSeconds()}; pass to {@code addVisionMeasurement}. */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /** The targets that contributed to the estimate. */
    public List<LumenTrackedTarget> getTargetsUsed() {
        return targetsUsed;
    }
}
