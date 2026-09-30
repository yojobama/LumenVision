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

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.function.Function;

/**
 * Turns a coprocessor result into the robot's field-relative pose, mirroring photonlib's PhotonPoseEstimator: construct it with the
 * field layout, a {@link LumenPoseStrategy} and the camera mount offset, then call {@link #update(LumenPipelineResult)} each loop.
 * Every strategy other than the coprocessor ones runs here from the targets' camera-to-tag transforms and the layout's tag poses.
 */
public class LumenPoseEstimator {
    private final LumenCamera camera;
    private AprilTagFieldLayout fieldTags;
    private LumenPoseStrategy primaryStrategy;
    private LumenPoseStrategy multiTagFallbackStrategy = LumenPoseStrategy.LOWEST_AMBIGUITY;
    private Transform3d robotToCamera;

    private Pose3d referencePose = new Pose3d();
    private Pose3d lastPose;
    private final TimeInterpolatableBuffer<Rotation2d> headingBuffer = TimeInterpolatableBuffer.createBuffer(1.0);

    /** @param camera read by {@link #update()}; may be null when results are passed to {@link #update(LumenPipelineResult)} */
    public LumenPoseEstimator(AprilTagFieldLayout fieldTags, LumenPoseStrategy strategy, LumenCamera camera, Transform3d robotToCamera) {
        this.fieldTags = fieldTags;
        this.primaryStrategy = strategy;
        this.camera = camera;
        this.robotToCamera = robotToCamera;
    }

    public LumenPoseEstimator(AprilTagFieldLayout fieldTags, LumenPoseStrategy strategy, Transform3d robotToCamera) {
        this(fieldTags, strategy, null, robotToCamera);
    }

    // ---- configuration ----

    public AprilTagFieldLayout getFieldTags() {
        return fieldTags;
    }

    public void setFieldTags(AprilTagFieldLayout fieldTags) {
        this.fieldTags = fieldTags;
    }

    public LumenPoseStrategy getPrimaryStrategy() {
        return primaryStrategy;
    }

    public void setPrimaryStrategy(LumenPoseStrategy strategy) {
        this.primaryStrategy = strategy;
    }

    /** The strategy used when {@link LumenPoseStrategy#MULTI_TAG_PNP_ON_COPROCESSOR} has no multi-tag result (default LOWEST_AMBIGUITY). */
    public void setMultiTagFallbackStrategy(LumenPoseStrategy strategy) {
        this.multiTagFallbackStrategy = strategy;
    }

    public LumenPoseStrategy getMultiTagFallbackStrategy() {
        return multiTagFallbackStrategy;
    }

    public Transform3d getRobotToCameraTransform() {
        return robotToCamera;
    }

    public void setRobotToCameraTransform(Transform3d robotToCamera) {
        this.robotToCamera = robotToCamera;
    }

    /** The pose {@link LumenPoseStrategy#CLOSEST_TO_REFERENCE_POSE} compares candidates with. */
    public void setReferencePose(Pose3d referencePose) {
        this.referencePose = referencePose;
    }

    public void setReferencePose(Pose2d referencePose) {
        this.referencePose = new Pose3d(referencePose);
    }

    /** The pose {@link LumenPoseStrategy#CLOSEST_TO_LAST_POSE} compares candidates with; updated by every successful estimate. */
    public void setLastPose(Pose3d lastPose) {
        this.lastPose = lastPose;
    }

    public void setLastPose(Pose2d lastPose) {
        this.lastPose = new Pose3d(lastPose);
    }

    /** Records the robot's heading at a moment, for {@link LumenPoseStrategy#PNP_DISTANCE_TRIG_SOLVE}; call every loop with the gyro. */
    public void addHeadingData(double timestampSeconds, Rotation2d heading) {
        headingBuffer.addSample(timestampSeconds, heading);
    }

    public void addHeadingData(double timestampSeconds, Rotation3d heading) {
        addHeadingData(timestampSeconds, heading.toRotation2d());
    }

    // ---- estimation ----

    /** Equivalent to {@code update(camera.getLatestResult())}; needs the camera given at construction. */
    public Optional<LumenEstimatedRobotPose> update() {
        if (camera == null) throw new IllegalStateException("this estimator has no camera; pass a result to update(result)");
        return update(camera.getLatestResult());
    }

    /** Estimates the robot pose from a result of this estimator's camera using the primary strategy (and the fallback, if needed). */
    public Optional<LumenEstimatedRobotPose> update(LumenPipelineResult result) {
        Optional<LumenEstimatedRobotPose> estimate = estimate(result, primaryStrategy);
        if (estimate.isEmpty() && primaryStrategy == LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR) {
            estimate = estimate(result, multiTagFallbackStrategy);
        }
        estimate.ifPresent(e -> lastPose = e.getEstimatedPose());
        return estimate;
    }

    private Optional<LumenEstimatedRobotPose> estimate(LumenPipelineResult result, LumenPoseStrategy strategy) {
        switch (strategy) {
            case LOWEST_AMBIGUITY:
                return estimateLowestAmbiguityPose(result);
            case CLOSEST_TO_CAMERA_HEIGHT:
                return estimateClosestToCameraHeightPose(result);
            case CLOSEST_TO_REFERENCE_POSE:
                return estimateClosestToReferencePose(result, referencePose);
            case CLOSEST_TO_LAST_POSE:
                return lastPose == null ? estimateLowestAmbiguityPose(result) : estimateClosestToReferencePose(result, lastPose, strategy);
            case AVERAGE_BEST_TARGETS:
                return estimateAverageBestTargetsPose(result);
            case MULTI_TAG_PNP_ON_COPROCESSOR:
                return estimateCoprocMultiTagPose(result);
            case PNP_DISTANCE_TRIG_SOLVE:
                return estimatePnpDistanceTrigSolvePose(result);
            case CONSTRAINED_SOLVEPNP:
                return Optional.empty(); // needs the coprocessor's constrained solve (added with LumenCamera's seed publishing)
            default:
                return Optional.empty();
        }
    }

    /** The coprocessor's multi-tag result composed with the camera mount; empty without one (fewer than 2 visible tags with known poses). */
    public Optional<LumenEstimatedRobotPose> estimateCoprocMultiTagPose(LumenPipelineResult result) {
        return result.getMultiTagResult().map(multiTag -> {
            Pose3d cameraPose = multiTag.getFieldToCamera();
            // the coprocessor solved in the layout's own frame; express it relative to the layout's current origin
            if (fieldTags != null) cameraPose = cameraPose.relativeTo(fieldTags.getOrigin());
            List<LumenTrackedTarget> used = new ArrayList<>();
            for (LumenTrackedTarget target : result.getTargets()) {
                if (multiTag.getFiducialIds().contains(target.getFiducialId())) used.add(target);
            }
            return new LumenEstimatedRobotPose(cameraPose.transformBy(robotToCamera.inverse()), result.getTimestampSeconds(), used,
                    LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR);
        });
    }

    /** The pose from the visible tag (with a known field pose) that has the lowest ambiguity. */
    public Optional<LumenEstimatedRobotPose> estimateLowestAmbiguityPose(LumenPipelineResult result) {
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
        return Optional.of(pose(result, LumenPoseStrategy.LOWEST_AMBIGUITY, robotPose(bestTagPose, best.getBestCameraToTarget()), List.of(best)));
    }

    /** The candidate (either pose hypothesis of any visible tag) whose camera height is closest to the camera's mount height. */
    public Optional<LumenEstimatedRobotPose> estimateClosestToCameraHeightPose(LumenPipelineResult result) {
        return closest(result, LumenPoseStrategy.CLOSEST_TO_CAMERA_HEIGHT,
                candidate -> Math.abs(robotToCamera.getZ() - candidate.cameraPose.getZ()));
    }

    /** The candidate closest (by translation) to a reference robot pose. */
    public Optional<LumenEstimatedRobotPose> estimateClosestToReferencePose(LumenPipelineResult result, Pose3d reference) {
        return estimateClosestToReferencePose(result, reference, LumenPoseStrategy.CLOSEST_TO_REFERENCE_POSE);
    }

    private Optional<LumenEstimatedRobotPose> estimateClosestToReferencePose(LumenPipelineResult result, Pose3d reference,
            LumenPoseStrategy strategy) {
        return closest(result, strategy, candidate -> candidate.robotPose.getTranslation().getDistance(reference.getTranslation()));
    }

    /** An average of every visible tag's pose, weighted by one over its ambiguity. */
    public Optional<LumenEstimatedRobotPose> estimateAverageBestTargetsPose(LumenPipelineResult result) {
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
        return Optional.of(pose(result, LumenPoseStrategy.AVERAGE_BEST_TARGETS, average, used));
    }

    /**
     * From the best tag's range and bearing plus the robot's heading at the frame's time ({@link #addHeadingData}): the robot
     * is placed so the tag appears where it was seen. Assumes the robot is on the floor (z = 0) and ignores the tag's orientation.
     */
    public Optional<LumenEstimatedRobotPose> estimatePnpDistanceTrigSolvePose(LumenPipelineResult result) {
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
        return Optional.of(pose(result, LumenPoseStrategy.PNP_DISTANCE_TRIG_SOLVE, robot, List.of(best.get())));
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
    private Optional<LumenEstimatedRobotPose> closest(LumenPipelineResult result, LumenPoseStrategy strategy, Function<Candidate, Double> score) {
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

    private static LumenEstimatedRobotPose pose(LumenPipelineResult result, LumenPoseStrategy strategy, Pose3d robotPose,
            List<LumenTrackedTarget> targets) {
        return new LumenEstimatedRobotPose(robotPose, result.getTimestampSeconds(), new ArrayList<>(targets), strategy);
    }

    private static double dot(Quaternion a, Quaternion b) {
        return a.getW() * b.getW() + a.getX() * b.getX() + a.getY() * b.getY() + a.getZ() * b.getZ();
    }
}
