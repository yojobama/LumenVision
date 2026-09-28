package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose3d;

/**
 * The robot's field-relative pose from one coprocessor multi-tag result, produced by {@link
 * LumenPoseEstimator}. Mirrors photonlib's {@code EstimatedRobotPose} without a {@code PoseStrategy}.
 */
public class LumenEstimatedRobotPose {
    private final Pose3d estimatedPose;
    private final double timestampSeconds;
    private final int tagCount;
    private final double reprojectionErrorPixels;

    LumenEstimatedRobotPose(Pose3d estimatedPose, double timestampSeconds, int tagCount, double reprojectionErrorPixels) {
        this.estimatedPose = estimatedPose;
        this.timestampSeconds = timestampSeconds;
        this.tagCount = tagCount;
        this.reprojectionErrorPixels = reprojectionErrorPixels;
    }

    /** The robot's field-relative pose: the camera pose composed with {@code robotToCamera}. */
    public Pose3d getEstimatedPose() {
        return estimatedPose;
    }

    /** Same clock domain as {@link LumenPipelineResult#getTimestampSeconds()}; pass to {@code addVisionMeasurement}. */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /** Number of tags in the multi-tag solve (always >= 2). */
    public int getTagCount() {
        return tagCount;
    }

    /** RMS reprojection error in pixels - see {@link LumenMultiTagResult#getReprojectionErrorPixels()}. */
    public double getReprojectionErrorPixels() {
        return reprojectionErrorPixels;
    }
}
