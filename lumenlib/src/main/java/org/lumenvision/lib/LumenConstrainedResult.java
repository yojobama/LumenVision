package org.lumenvision.lib;

import edu.wpi.first.math.geometry.Pose2d;

/**
 * The coprocessor's floor-constrained solve: the robot's own field pose found from every visible tag with a known field pose, assuming
 * the robot stays flat on the floor and the camera sits at the mount offset the robot published. One visible tag is enough.
 */
public class LumenConstrainedResult {
    private final Pose2d robotPose;
    private final double reprojectionErrorPixels;
    private final int tagCount;
    private final double timestampSeconds;
    private final long latencyMicros;

    /** A result with no capture time ({@link #getTimestampSeconds()} 0), e.g. for a test. */
    public LumenConstrainedResult(Pose2d robotPose, double reprojectionErrorPixels, int tagCount) {
        this(robotPose, reprojectionErrorPixels, tagCount, 0.0, 0);
    }

    /**
     * @param timestampSeconds the frame's capture time (see {@link #getTimestampSeconds()})
     * @param latencyMicros its capture-to-publish latency
     */
    public LumenConstrainedResult(Pose2d robotPose, double reprojectionErrorPixels, int tagCount, double timestampSeconds, long latencyMicros) {
        this.timestampSeconds = timestampSeconds;
        this.latencyMicros = latencyMicros;
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

    /**
     * When the frame this came from was captured, in seconds on the NetworkTables server clock (the roboRIO's FPGA time): the value
     * {@code addVisionMeasurement} wants. Same as the {@link LumenPipelineResult} it came with; 0 for one built without a time.
     */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /** Capture-to-publish latency of that frame in milliseconds; 0 for one built without a time. */
    public double getLatencyMillis() {
        return latencyMicros / 1000.0;
    }
}
