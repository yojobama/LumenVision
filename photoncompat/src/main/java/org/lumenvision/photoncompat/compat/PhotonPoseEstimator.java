package org.lumenvision.photoncompat.compat;

import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Transform3d;
import org.lumenvision.photoncompat.LumenPoseEstimator;
import org.lumenvision.photoncompat.LumenPoseStrategy;

import java.util.Optional;

/** {@link LumenPoseEstimator} with photonlib's PhotonPoseEstimator method names, for drop-in migration. */
public class PhotonPoseEstimator {
    private final LumenPoseEstimator lumenEstimator;
    private final PhotonCamera camera;

    /** The current photonlib constructor: pass each camera's result to {@link #update(PhotonPipelineResult)}. */
    public PhotonPoseEstimator(AprilTagFieldLayout fieldTags, PoseStrategy strategy, Transform3d robotToCamera) {
        this.camera = null;
        this.lumenEstimator = new LumenPoseEstimator(fieldTags, strategy.toLumen(), robotToCamera);
    }

    /** The older photonlib constructor that also takes the camera, enabling {@link #update()}. */
    public PhotonPoseEstimator(AprilTagFieldLayout fieldTags, PoseStrategy strategy, PhotonCamera camera, Transform3d robotToCamera) {
        this.camera = camera;
        this.lumenEstimator = new LumenPoseEstimator(fieldTags, strategy.toLumen(), camera.getLumenCamera(), robotToCamera);
    }

    public Optional<EstimatedRobotPose> update() {
        if (camera == null) throw new IllegalStateException("this estimator has no camera; pass a result to update(result)");
        return lumenEstimator.update().map(EstimatedRobotPose::from);
    }

    public Optional<EstimatedRobotPose> update(PhotonPipelineResult result) {
        return lumenEstimator.update(result.getLumenPipelineResult()).map(EstimatedRobotPose::from);
    }

    public Optional<EstimatedRobotPose> estimateCoprocMultiTagPose(PhotonPipelineResult result) {
        return lumenEstimator.estimateCoprocMultiTagPose(result.getLumenPipelineResult()).map(EstimatedRobotPose::from);
    }

    public Optional<EstimatedRobotPose> estimateLowestAmbiguityPose(PhotonPipelineResult result) {
        return lumenEstimator.estimateLowestAmbiguityPose(result.getLumenPipelineResult()).map(EstimatedRobotPose::from);
    }

    public Optional<EstimatedRobotPose> estimateClosestToCameraHeightPose(PhotonPipelineResult result) {
        return lumenEstimator.estimateClosestToCameraHeightPose(result.getLumenPipelineResult()).map(EstimatedRobotPose::from);
    }

    public Optional<EstimatedRobotPose> estimateClosestToReferencePose(PhotonPipelineResult result, Pose3d referencePose) {
        return lumenEstimator.estimateClosestToReferencePose(result.getLumenPipelineResult(), referencePose).map(EstimatedRobotPose::from);
    }

    public Optional<EstimatedRobotPose> estimateAverageBestTargetsPose(PhotonPipelineResult result) {
        return lumenEstimator.estimateAverageBestTargetsPose(result.getLumenPipelineResult()).map(EstimatedRobotPose::from);
    }

    public Optional<EstimatedRobotPose> estimatePnpDistanceTrigSolvePose(PhotonPipelineResult result) {
        return lumenEstimator.estimatePnpDistanceTrigSolvePose(result.getLumenPipelineResult()).map(EstimatedRobotPose::from);
    }

    public void addHeadingData(double timestampSeconds, Rotation2d heading) {
        lumenEstimator.addHeadingData(timestampSeconds, heading);
    }

    public void setReferencePose(Pose3d referencePose) {
        lumenEstimator.setReferencePose(referencePose);
    }

    public void setReferencePose(Pose2d referencePose) {
        lumenEstimator.setReferencePose(referencePose);
    }

    public void setLastPose(Pose3d lastPose) {
        lumenEstimator.setLastPose(lastPose);
    }

    public void setLastPose(Pose2d lastPose) {
        lumenEstimator.setLastPose(lastPose);
    }

    public PoseStrategy getPrimaryStrategy() {
        return PoseStrategy.fromLumen(lumenEstimator.getPrimaryStrategy());
    }

    public void setPrimaryStrategy(PoseStrategy strategy) {
        lumenEstimator.setPrimaryStrategy(strategy.toLumen());
    }

    public void setMultiTagFallbackStrategy(PoseStrategy strategy) {
        lumenEstimator.setMultiTagFallbackStrategy(strategy.toLumen());
    }

    public AprilTagFieldLayout getFieldTags() {
        return lumenEstimator.getFieldTags();
    }

    public void setFieldTags(AprilTagFieldLayout fieldTags) {
        lumenEstimator.setFieldTags(fieldTags);
    }

    public Transform3d getRobotToCameraTransform() {
        return lumenEstimator.getRobotToCameraTransform();
    }

    public void setRobotToCameraTransform(Transform3d robotToCamera) {
        lumenEstimator.setRobotToCameraTransform(robotToCamera);
    }

    public LumenPoseEstimator getLumenPoseEstimator() {
        return lumenEstimator;
    }
}
