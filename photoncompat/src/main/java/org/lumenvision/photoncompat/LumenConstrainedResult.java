package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose2d;

/**
 * The coprocessor's floor-constrained solve: the robot's own field pose found from every visible tag with a known field pose, assuming
 * the robot stays flat on the floor and the camera sits at the mount offset the robot published. One visible tag is enough.
 */
public class LumenConstrainedResult {
    private final Pose2d robotPose;
    private final double reprojectionErrorPixels;
    private final int tagCount;

    LumenConstrainedResult(Pose2d robotPose, double reprojectionErrorPixels, int tagCount) {
        this.robotPose = robotPose;
        this.reprojectionErrorPixels = reprojectionErrorPixels;
        this.tagCount = tagCount;
    }

    /** The robot's pose in the coprocessor's field-layout frame (not relative to the layout's current origin). */
    public Pose2d getRobotPoseInLayoutFrame() {
        return robotPose;
    }

    /** RMS reprojection error in pixels across the tags' corners. */
    public double getReprojectionErrorPixels() {
        return reprojectionErrorPixels;
    }

    /** Number of tags in the solve. */
    public int getTagCount() {
        return tagCount;
    }
}
