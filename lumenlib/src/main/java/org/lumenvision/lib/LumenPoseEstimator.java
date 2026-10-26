package org.lumenvision.lib;

import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Transform3d;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/**
 * Turns the coprocessor's own solves into the robot's field-relative pose: the multi-tag solve over every visible tag, and the
 * floor-constrained solve. Both are computed on the coprocessor, so this only expresses them in the robot's frame.
 *
 * <p>The coprocessor solves in the field layout's own frame; give the layout the robot uses (or null when it is the same one) so
 * the pose is expressed relative to that layout's current origin.
 */
public class LumenPoseEstimator {
    private AprilTagFieldLayout fieldTags;
    private Transform3d robotToCamera;

    public LumenPoseEstimator(AprilTagFieldLayout fieldTags, Transform3d robotToCamera) {
        this.fieldTags = fieldTags;
        this.robotToCamera = robotToCamera;
    }

    public AprilTagFieldLayout getFieldTags() {
        return fieldTags;
    }

    public void setFieldTags(AprilTagFieldLayout fieldTags) {
        this.fieldTags = fieldTags;
    }

    public Transform3d getRobotToCameraTransform() {
        return robotToCamera;
    }

    public void setRobotToCameraTransform(Transform3d robotToCamera) {
        this.robotToCamera = robotToCamera;
    }

    /** The coprocessor's multi-tag result composed with the camera mount; empty without one (fewer than 2 visible tags with known poses). */
    public Optional<LumenRobotPose> estimateMultiTag(LumenPipelineResult result) {
        return result.getMultiTagResult().map(multiTag -> {
            Pose3d cameraPose = multiTag.getFieldToCamera();
            if (fieldTags != null) cameraPose = cameraPose.relativeTo(fieldTags.getOrigin());
            List<LumenTrackedTarget> used = new ArrayList<>();
            for (LumenTrackedTarget target : result.getTargets()) {
                if (multiTag.getFiducialIds().contains(target.getFiducialId())) used.add(target);
            }
            return new LumenRobotPose(cameraPose.transformBy(robotToCamera.inverse()), result.getTimestampSeconds(), used);
        });
    }

    /** The coprocessor's floor-constrained solve (one visible tag is enough); empty until a seed has been published with {@link #publishSeed}. */
    public Optional<LumenRobotPose> estimateConstrained(LumenPipelineResult result) {
        return result.getConstrainedResult().map(constrained -> {
            Pose3d robotPose = new Pose3d(constrained.getRobotPoseInLayoutFrame());
            if (fieldTags != null) robotPose = robotPose.relativeTo(fieldTags.getOrigin());
            return new LumenRobotPose(robotPose, result.getTimestampSeconds(), new ArrayList<>(result.getTargets()));
        });
    }

    /** Sends the detector the starting robot pose for the constrained solve, expressed in the layout's own frame. */
    public void publishSeed(LumenDetector detector, Pose3d robotPose) {
        Pose3d seed = fieldTags != null ? fieldTags.getOrigin().plus(new Transform3d(new Pose3d(), robotPose)) : robotPose;
        detector.setConstrainedSeed(seed.toPose2d(), robotToCamera);
    }
}
