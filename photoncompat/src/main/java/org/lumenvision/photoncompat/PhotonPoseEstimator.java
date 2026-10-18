package org.lumenvision.photoncompat;

import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Quaternion;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation2d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.math.interpolation.TimeInterpolatableBuffer;
import org.lumenvision.lib.LumenPipelineResult;
import org.lumenvision.lib.LumenPoseEstimator;
import org.lumenvision.lib.LumenRobotPose;
import org.lumenvision.lib.LumenTrackedTarget;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.function.Function;
import java.util.stream.Collectors;

/**
 * photonlib's PhotonPoseEstimator: construct it with the field layout, a {@link PoseStrategy} and the camera mount offset, then pass
 * each result to {@link #update(PhotonPipelineResult)}. The multi-tag and constrained strategies come from the coprocessor
 * ({@link LumenPoseEstimator}); every other strategy runs here from the targets' camera-to-tag transforms and the layout's tag poses.
 */
public class PhotonPoseEstimator {
    private final LumenPoseEstimator coprocessorEstimator;
    private final PhotonCamera camera;
    private AprilTagFieldLayout fieldTags;
    private PoseStrategy primaryStrategy;
    private PoseStrategy multiTagFallbackStrategy = PoseStrategy.LOWEST_AMBIGUITY;
    private Transform3d robotToCamera;

    private Pose3d referencePose = new Pose3d();
    private Pose3d lastPose;
    private final TimeInterpolatableBuffer<Rotation2d> headingBuffer = TimeInterpolatableBuffer.createBuffer(1.0);

    /** The current photonlib constructor: pass each camera's result to {@link #update(PhotonPipelineResult)}. */
    public PhotonPoseEstimator(AprilTagFieldLayout fieldTags, PoseStrategy strategy, Transform3d robotToCamera) {
        this(fieldTags, strategy, null, robotToCamera);
    }

    /** The older photonlib constructor that also takes the camera, enabling {@link #update()}. */
    public PhotonPoseEstimator(AprilTagFieldLayout fieldTags, PoseStrategy strategy, PhotonCamera camera, Transform3d robotToCamera) {
        this.fieldTags = fieldTags;
        this.primaryStrategy = strategy;
        this.camera = camera;
        this.robotToCamera = robotToCamera;
        this.coprocessorEstimator = new LumenPoseEstimator(fieldTags, robotToCamera);
    }

    // ---- configuration ----

    public AprilTagFieldLayout getFieldTags() {
        return fieldTags;
    }

    public void setFieldTags(AprilTagFieldLayout fieldTags) {
        this.fieldTags = fieldTags;
        coprocessorEstimator.setFieldTags(fieldTags);
    }

    public PoseStrategy getPrimaryStrategy() {
        return primaryStrategy;
    }

    public void setPrimaryStrategy(PoseStrategy strategy) {
        this.primaryStrategy = strategy;
    }

    /** The strategy used when a coprocessor strategy has no result (default LOWEST_AMBIGUITY). */
    public void setMultiTagFallbackStrategy(PoseStrategy strategy) {
        this.multiTagFallbackStrategy = strategy;
    }

    public PoseStrategy getMultiTagFallbackStrategy() {
        return multiTagFallbackStrategy;
    }

    public Transform3d getRobotToCameraTransform() {
        return robotToCamera;
    }

    public void setRobotToCameraTransform(Transform3d robotToCamera) {
        this.robotToCamera = robotToCamera;
        coprocessorEstimator.setRobotToCameraTransform(robotToCamera);
    }

    /** The pose CLOSEST_TO_REFERENCE_POSE compares candidates with. */
    public void setReferencePose(Pose3d referencePose) {
        this.referencePose = referencePose;
    }

    public void setReferencePose(Pose2d referencePose) {
        this.referencePose = new Pose3d(referencePose);
    }

    /** The pose CLOSEST_TO_LAST_POSE compares candidates with; updated by every successful estimate. */
    public void setLastPose(Pose3d lastPose) {
        this.lastPose = lastPose;
    }

    public void setLastPose(Pose2d lastPose) {
        this.lastPose = new Pose3d(lastPose);
    }

    /** Records the robot's heading at a moment, for PNP_DISTANCE_TRIG_SOLVE; call every loop with the gyro. */
    public void addHeadingData(double timestampSeconds, Rotation2d heading) {
        headingBuffer.addSample(timestampSeconds, heading);
    }

    public void addHeadingData(double timestampSeconds, Rotation3d heading) {
        addHeadingData(timestampSeconds, heading.toRotation2d());
    }

    // ---- estimation ----

    /** Equivalent to {@code update(camera.getLatestResult())}; needs the camera given at construction. */
    public Optional<EstimatedRobotPose> update() {
        if (camera == null) throw new IllegalStateException("this estimator has no camera; pass a result to update(result)");
        if (primaryStrategy == PoseStrategy.CONSTRAINED_SOLVEPNP) {
            coprocessorEstimator.publishSeed(camera.getLumenDetector(), lastPose != null ? lastPose : referencePose);
        }
        return update(camera.getLatestResult());
    }

    /** Estimates the robot pose from a result of this estimator's camera using the primary strategy (and the fallback, if needed). */
    public Optional<EstimatedRobotPose> update(PhotonPipelineResult photonResult) {
        LumenPipelineResult result = photonResult.getLumenPipelineResult();
        Optional<EstimatedRobotPose> estimate = estimate(result, primaryStrategy);
        if (estimate.isEmpty() && (primaryStrategy == PoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR
                || primaryStrategy == PoseStrategy.MULTI_TAG_PNP_ON_RIO || primaryStrategy == PoseStrategy.CONSTRAINED_SOLVEPNP)) {
            estimate = estimate(result, multiTagFallbackStrategy);
        }
        estimate.ifPresent(e -> lastPose = e.estimatedPose);
        return estimate;
    }

    private Optional<EstimatedRobotPose> estimate(LumenPipelineResult result, PoseStrategy strategy) {
        switch (strategy) {
            case LOWEST_AMBIGUITY:
                return lowestAmbiguity(result);
            case CLOSEST_TO_CAMERA_HEIGHT:
                return closestToCameraHeight(result);
            case CLOSEST_TO_REFERENCE_POSE:
                return closestToReference(result, referencePose, strategy);
            case CLOSEST_TO_LAST_POSE:
                return lastPose == null ? lowestAmbiguity(result) : closestToReference(result, lastPose, strategy);
            case AVERAGE_BEST_TARGETS:
                return averageBestTargets(result);
            case MULTI_TAG_PNP_ON_COPROCESSOR:
            case MULTI_TAG_PNP_ON_RIO:
                return coprocessorEstimator.estimateMultiTag(result).map(p -> from(p, strategy));
            case PNP_DISTANCE_TRIG_SOLVE:
                return pnpDistanceTrigSolve(result);
            case CONSTRAINED_SOLVEPNP:
                return coprocessorEstimator.estimateConstrained(result).map(p -> from(p, strategy));
            default:
                return Optional.empty();
        }
    }

    public Optional<EstimatedRobotPose> estimateCoprocMultiTagPose(PhotonPipelineResult result) {
        return estimate(result.getLumenPipelineResult(), PoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR);
    }

    public Optional<EstimatedRobotPose> estimateLowestAmbiguityPose(PhotonPipelineResult result) {
        return lowestAmbiguity(result.getLumenPipelineResult());
    }

    public Optional<EstimatedRobotPose> estimateClosestToCameraHeightPose(PhotonPipelineResult result) {
        return closestToCameraHeight(result.getLumenPipelineResult());
    }

    public Optional<EstimatedRobotPose> estimateClosestToReferencePose(PhotonPipelineResult result, Pose3d reference) {
        return closestToReference(result.getLumenPipelineResult(), reference, PoseStrategy.CLOSEST_TO_REFERENCE_POSE);
    }

    public Optional<EstimatedRobotPose> estimateAverageBestTargetsPose(PhotonPipelineResult result) {
        return averageBestTargets(result.getLumenPipelineResult());
    }

    public Optional<EstimatedRobotPose> estimatePnpDistanceTrigSolvePose(PhotonPipelineResult result) {
        return pnpDistanceTrigSolve(result.getLumenPipelineResult());
    }

    // ---- strategies run on the roboRIO ----

    private Optional<EstimatedRobotPose> lowestAmbiguity(LumenPipelineResult result) {
        LumenTrackedTarget best = null;
        Pose3d bestTagPose = null;
        for (LumenTrackedTarget target : result.getTargets()) {
            Optional<Pose3d> tagPose = tagPose(target);
            if (tagPose.isEmpty() || target.getPoseAmbiguity() < 0) continue;
            if (best == null || target.getPoseAmbiguity() < best.getPoseAmbiguity()) {
                best = target;
                bestTagPose = tagPose.get();
            }
        }
        if (best == null) return Optional.empty();
        return Optional.of(pose(result, PoseStrategy.LOWEST_AMBIGUITY, robotPose(bestTagPose, best.getBestCameraToTarget()), List.of(best)));
    }

    // the candidate (either pose hypothesis of any visible tag) whose camera height is closest to the camera's mount height
    private Optional<EstimatedRobotPose> closestToCameraHeight(LumenPipelineResult result) {
        return closest(result, PoseStrategy.CLOSEST_TO_CAMERA_HEIGHT, candidate -> Math.abs(robotToCamera.getZ() - candidate.cameraPose.getZ()));
    }

    private Optional<EstimatedRobotPose> closestToReference(LumenPipelineResult result, Pose3d reference, PoseStrategy strategy) {
        return closest(result, strategy, candidate -> candidate.robotPose.getTranslation().getDistance(reference.getTranslation()));
    }

    // an average of every visible tag's pose, weighted by one over its ambiguity
    private Optional<EstimatedRobotPose> averageBestTargets(LumenPipelineResult result) {
        List<LumenTrackedTarget> used = new ArrayList<>();
        List<Pose3d> poses = new ArrayList<>();
        List<Double> weights = new ArrayList<>();
        for (LumenTrackedTarget target : result.getTargets()) {
            Optional<Pose3d> tagPose = tagPose(target);
            if (tagPose.isEmpty() || target.getPoseAmbiguity() < 0) continue;
            used.add(target);
            poses.add(robotPose(tagPose.get(), target.getBestCameraToTarget()));
            weights.add(1.0 / Math.max(target.getPoseAmbiguity(), 1e-5));
        }
        if (poses.isEmpty()) return Optional.empty();

        double total = weights.stream().mapToDouble(Double::doubleValue).sum();
        double x = 0, y = 0, z = 0;
        double qw = 0, qx = 0, qy = 0, qz = 0;
        Quaternion first = poses.get(0).getRotation().getQuaternion();
        for (int i = 0; i < poses.size(); i++) {
            double w = weights.get(i) / total;
            Translation3d t = poses.get(i).getTranslation();
            x += w * t.getX();
            y += w * t.getY();
            z += w * t.getZ();
            Quaternion q = poses.get(i).getRotation().getQuaternion();
            // q and -q are the same rotation; align signs before summing
            double sign = dot(first, q) < 0 ? -1.0 : 1.0;
            qw += w * sign * q.getW();
            qx += w * sign * q.getX();
            qy += w * sign * q.getY();
            qz += w * sign * q.getZ();
        }
        Pose3d average = new Pose3d(new Translation3d(x, y, z), new Rotation3d(new Quaternion(qw, qx, qy, qz).normalize()));
        return Optional.of(pose(result, PoseStrategy.AVERAGE_BEST_TARGETS, average, used));
    }

    // from the best tag's range and bearing plus the robot's heading at the frame's time: the robot is placed so the tag appears where
    // it was seen, on the floor (z = 0), ignoring the tag's orientation
    private Optional<EstimatedRobotPose> pnpDistanceTrigSolve(LumenPipelineResult result) {
        Optional<LumenTrackedTarget> best = result.getBestTarget();
        if (best.isEmpty()) return Optional.empty();
        Optional<Pose3d> tagPose = tagPose(best.get());
        Optional<Rotation2d> heading = headingBuffer.getSample(result.getTimestampSeconds());
        if (tagPose.isEmpty() || heading.isEmpty()) return Optional.empty();

        Translation3d tagInCamera = best.get().getBestCameraToTarget().getTranslation();
        Translation3d tagInRobot = tagInCamera.rotateBy(robotToCamera.getRotation()).plus(robotToCamera.getTranslation());
        Translation2d tagOffsetInField = tagInRobot.toTranslation2d().rotateBy(heading.get());
        Translation2d robotXY = tagPose.get().getTranslation().toTranslation2d().minus(tagOffsetInField);
        Pose3d robot = new Pose3d(robotXY.getX(), robotXY.getY(), 0.0, new Rotation3d(0, 0, heading.get().getRadians()));
        return Optional.of(pose(result, PoseStrategy.PNP_DISTANCE_TRIG_SOLVE, robot, List.of(best.get())));
    }

    // ---- helpers ----

    private static final class Candidate {
        final LumenTrackedTarget target;
        final Pose3d cameraPose;
        final Pose3d robotPose;

        Candidate(LumenTrackedTarget target, Pose3d cameraPose, Pose3d robotPose) {
            this.target = target;
            this.cameraPose = cameraPose;
            this.robotPose = robotPose;
        }
    }

    // both pose hypotheses of every visible tag with a known field pose, scored by `score` (lower wins)
    private Optional<EstimatedRobotPose> closest(LumenPipelineResult result, PoseStrategy strategy, Function<Candidate, Double> score) {
        Candidate winner = null;
        double winnerScore = Double.MAX_VALUE;
        for (LumenTrackedTarget target : result.getTargets()) {
            Optional<Pose3d> tagPose = tagPose(target);
            if (tagPose.isEmpty() || target.getPoseAmbiguity() < 0) continue;
            for (Transform3d cameraToTarget : new Transform3d[] { target.getBestCameraToTarget(), target.getAlternateCameraToTarget() }) {
                Pose3d cameraPose = tagPose.get().transformBy(cameraToTarget.inverse());
                Candidate candidate = new Candidate(target, cameraPose, cameraPose.transformBy(robotToCamera.inverse()));
                double s = score.apply(candidate);
                if (s < winnerScore) {
                    winnerScore = s;
                    winner = candidate;
                }
            }
        }
        if (winner == null) return Optional.empty();
        return Optional.of(pose(result, strategy, winner.robotPose, List.of(winner.target)));
    }

    private Optional<Pose3d> tagPose(LumenTrackedTarget target) {
        if (fieldTags == null) throw new IllegalStateException("this pose strategy needs an AprilTagFieldLayout");
        return target.getFiducialId() < 0 ? Optional.empty() : fieldTags.getTagPose(target.getFiducialId());
    }

    private Pose3d robotPose(Pose3d tagFieldPose, Transform3d cameraToTarget) {
        return tagFieldPose.transformBy(cameraToTarget.inverse()).transformBy(robotToCamera.inverse());
    }

    private static EstimatedRobotPose pose(LumenPipelineResult result, PoseStrategy strategy, Pose3d robotPose, List<LumenTrackedTarget> targets) {
        return new EstimatedRobotPose(robotPose, result.getTimestampSeconds(),
                targets.stream().map(PhotonTrackedTarget::new).collect(Collectors.toList()), strategy);
    }

    private static EstimatedRobotPose from(LumenRobotPose pose, PoseStrategy strategy) {
        return new EstimatedRobotPose(pose.getPose(), pose.getTimestampSeconds(),
                pose.getTargetsUsed().stream().map(PhotonTrackedTarget::new).collect(Collectors.toList()), strategy);
    }

    private static double dot(Quaternion a, Quaternion b) {
        return a.getW() * b.getW() + a.getX() * b.getX() + a.getY() * b.getY() + a.getZ() * b.getZ();
    }
}
