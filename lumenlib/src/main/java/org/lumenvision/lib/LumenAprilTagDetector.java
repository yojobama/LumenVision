package org.lumenvision.lib;

import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.networktables.NetworkTableInstance;

import java.util.Optional;

/**
 * An AprilTag detector node of the coprocessor's graph. Each result has the tags seen in a frame and, on a calibrated camera, their poses
 * and the coprocessor's pose solves over them: the multi-tag solve, and the floor-constrained solve once {@link #setConstrainedSeed} has
 * been called. {@link LumenPoseEstimator} turns either into the robot's field pose.
 *
 * <pre>{@code
 * LumenAprilTagDetector front = new LumenAprilTagDetector("front");
 * for (LumenAprilTagResult result : front.getAllUnreadResults()) {
 *     estimator.estimateMultiTag(result).ifPresent(pose -> drive.addVisionMeasurement(pose.getPose().toPose2d(), pose.getTimestampSeconds()));
 * }
 * }</pre>
 */
public class LumenAprilTagDetector extends LumenDetector<LumenAprilTagResult> {
    /**
     * The default NT4 instance and root table "lumenvision".
     *
     * @param name the node's name in the LumenVision UI (or the NT table its topics are under)
     */
    public LumenAprilTagDetector(String name) {
        this(NetworkTableInstance.getDefault(), LumenCoprocessor.DEFAULT_ROOT_TABLE, name);
    }

    /**
     * @param instance the NetworkTableInstance to use (injectable for simulation)
     * @param rootTable must match the coprocessor's NetworkTablesConfig.rootTable (default "lumenvision")
     * @param name the node's name in the LumenVision UI (or the NT table its topics are under)
     */
    public LumenAprilTagDetector(NetworkTableInstance instance, String rootTable, String name) {
        super(instance, rootTable, name, LumenGraph.APRILTAG_DETECTOR);
    }

    @Override
    protected LumenAprilTagResult toResult(LumenPipelineResult packet) {
        return LumenAprilTagResult.from(packet);
    }

    /** The multi-tag result of the latest frame, if the coprocessor solved one; it carries that frame's capture time. */
    public Optional<LumenMultiTagResult> getMultiTagResult() {
        return getLatestResult().getMultiTagResult();
    }

    /**
     * Starts (and keeps current) the coprocessor's floor-constrained solve: the starting robot pose, in the coprocessor's field-layout
     * frame, and where the camera is mounted on the robot. Call every loop with the latest estimate; the result appears in
     * {@link LumenAprilTagResult#getConstrainedResult()}. {@link LumenPoseEstimator#publishSeed} converts from the robot's field frame.
     */
    public void setConstrainedSeed(Pose2d robotPoseInLayoutFrame, Transform3d robotToCamera) {
        publishConstrainedSeed(robotPoseInLayoutFrame, robotToCamera);
    }
}
