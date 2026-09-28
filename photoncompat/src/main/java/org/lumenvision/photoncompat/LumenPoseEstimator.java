package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Transform3d;

import java.util.Optional;

/**
 * Turns the coprocessor's multi-tag PnP result into the robot's field-relative pose. Mirrors
 * photonlib's {@code PhotonPoseEstimator} (construct with the camera mount offset, then {@code
 * update(result)} each loop) but only composes {@link LumenMultiTagResult#getFieldToCamera()} with
 * {@code robotToCamera}; the solve is done on the coprocessor.
 */
public class LumenPoseEstimator {
    private final LumenCamera camera;
    private final Transform3d cameraToRobot;

    /**
     * @param camera the coprocessor node to read multi-tag results from
     * @param robotToCamera the camera's mount offset (robot origin to camera); inverted here relative
     *     to {@link LumenUtils#estimateFieldToRobotAprilTag}'s {@code cameraToRobot}
     */
    public LumenPoseEstimator(LumenCamera camera, Transform3d robotToCamera) {
        this.camera = camera;
        this.cameraToRobot = robotToCamera.inverse();
    }

    /** Equivalent to {@code update(camera.getLatestResult())}. */
    public Optional<LumenEstimatedRobotPose> update() {
        return update(camera.getLatestResult());
    }

    /**
     * @param result a snapshot from this estimator's {@link #camera}; another camera's result gives a wrong pose
     * @return empty when {@code result}'s multi-tag result is empty (fewer than 2 visible tags with known poses)
     */
    public Optional<LumenEstimatedRobotPose> update(LumenPipelineResult result) {
        return compose(result, cameraToRobot);
    }

    // package-private static so the composition maths is testable without a live LumenCamera
    static Optional<LumenEstimatedRobotPose> compose(LumenPipelineResult result, Transform3d cameraToRobot) {
        return result.getMultiTagResult().map(multiTag -> new LumenEstimatedRobotPose(
                multiTag.getFieldToCamera().transformBy(cameraToRobot),
                result.getTimestampSeconds(),
                multiTag.getTagCount(),
                multiTag.getReprojectionErrorPixels()));
    }
}
