package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.apriltag.AprilTag;
import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import org.junit.jupiter.api.Test;

// Builds results from known robot and tag poses (the inverse of what an estimator does) and checks each strategy recovers the robot.
class LumenPoseEstimatorTest {
    // tag 1 on the far wall facing the robot (yaw 180), tag 2 to its left facing back along -X and angled
    private static final Pose3d TAG1 = new Pose3d(8.0, 1.0, 1.2, new Rotation3d(0, 0, Math.PI));
    private static final Pose3d TAG2 = new Pose3d(8.0, 3.0, 0.9, new Rotation3d(0, 0, Math.PI - 0.4));
    private static final AprilTagFieldLayout LAYOUT = new AprilTagFieldLayout(
            List.of(new AprilTag(1, TAG1), new AprilTag(2, TAG2)), 16.0, 8.0);

    // the camera sits 0.3 m forward, 0.2 m up on the robot, pitched up 0.2 rad
    private static final Transform3d ROBOT_TO_CAMERA = new Transform3d(new Translation3d(0.3, 0.0, 0.2), new Rotation3d(0, -0.2, 0));
    private static final Pose3d ROBOT = new Pose3d(3.0, 1.5, 0.0, new Rotation3d(0, 0, 0.3));

    private static LumenTrackedTarget target(int id, Pose3d tag, Pose3d robot, double ambiguity, double area, Transform3d alternate) {
        Pose3d camera = robot.transformBy(ROBOT_TO_CAMERA);
        Transform3d best = new Transform3d(camera, tag);
        return new LumenTrackedTarget(id, -1, -1, 0, 0, area, 0, ambiguity, best, alternate, 0.2f, 0.9f, new double[8], new double[8]);
    }

    private static LumenPipelineResult result(Optional<LumenMultiTagResult> multiTag, LumenTrackedTarget... targets) {
        return new LumenPipelineResult(new ArrayList<>(List.of(targets)), 12.0, 7, 15000, multiTag);
    }

    private static void assertPose(Pose3d expected, Pose3d actual, double tolerance) {
        assertEquals(expected.getX(), actual.getX(), tolerance);
        assertEquals(expected.getY(), actual.getY(), tolerance);
        assertEquals(expected.getZ(), actual.getZ(), tolerance);
        assertEquals(0.0, expected.getRotation().minus(actual.getRotation()).getAngle(), tolerance);
    }

    private static LumenPoseEstimator estimator(LumenPoseStrategy strategy) {
        return new LumenPoseEstimator(LAYOUT, strategy, ROBOT_TO_CAMERA);
    }

    // a wrong hypothesis: the robot a metre off and turned, as planar-PnP's mirrored solution would put it
    private static Transform3d mirroredAlternate(Pose3d tag) {
        Pose3d wrongRobot = new Pose3d(2.0, 0.5, 0.4, new Rotation3d(0.1, 0.2, 0.9));
        return new Transform3d(wrongRobot.transformBy(ROBOT_TO_CAMERA), tag);
    }

    @Test
    void lowestAmbiguityPicksTheTagWithTheLowestAmbiguity() {
        LumenTrackedTarget unsure = target(1, TAG1, new Pose3d(9, 9, 0, new Rotation3d()), 0.8, 2.0, new Transform3d());
        LumenTrackedTarget sure = target(2, TAG2, ROBOT, 0.05, 1.0, new Transform3d());

        Optional<LumenEstimatedRobotPose> pose = estimator(LumenPoseStrategy.LOWEST_AMBIGUITY).update(result(Optional.empty(), unsure, sure));

        assertTrue(pose.isPresent());
        assertPose(ROBOT, pose.get().getEstimatedPose(), 1e-6);
        assertEquals(List.of(sure), pose.get().getTargetsUsed());
        assertEquals(LumenPoseStrategy.LOWEST_AMBIGUITY, pose.get().getStrategy());
        assertEquals(12.0, pose.get().getTimestampSeconds(), 1e-9);
    }

    @Test
    void unknownTagsAndTargetsWithoutAPoseAreSkipped() {
        LumenTrackedTarget unknownTag = target(99, TAG1, ROBOT, 0.0, 1.0, new Transform3d());
        LumenTrackedTarget noPose = target(1, TAG1, ROBOT, -1.0, 1.0, new Transform3d());

        assertTrue(estimator(LumenPoseStrategy.LOWEST_AMBIGUITY).update(result(Optional.empty(), unknownTag, noPose)).isEmpty());
        assertTrue(estimator(LumenPoseStrategy.LOWEST_AMBIGUITY).update(result(Optional.empty())).isEmpty());
    }

    @Test
    void closestToReferencePoseChoosesBetweenBothHypotheses() {
        // the tag's best hypothesis is wrong; the alternate is the real robot
        LumenTrackedTarget t = target(1, TAG1, new Pose3d(2.0, 0.5, 0.4, new Rotation3d(0.1, 0.2, 0.9)), 0.4, 1.0,
                new Transform3d(ROBOT.transformBy(ROBOT_TO_CAMERA), TAG1));
        LumenPoseEstimator estimator = estimator(LumenPoseStrategy.CLOSEST_TO_REFERENCE_POSE);
        estimator.setReferencePose(new Pose3d(3.1, 1.4, 0.0, new Rotation3d()));

        Optional<LumenEstimatedRobotPose> pose = estimator.update(result(Optional.empty(), t));

        assertPose(ROBOT, pose.orElseThrow().getEstimatedPose(), 1e-6);
    }

    @Test
    void closestToCameraHeightPrefersTheHypothesisAtTheMountHeight() {
        // best hypothesis puts the robot 0.4 m in the air; the alternate has it on the floor
        LumenTrackedTarget t = target(1, TAG1, new Pose3d(2.0, 0.5, 0.4, new Rotation3d(0.1, 0.2, 0.9)), 0.4, 1.0,
                new Transform3d(ROBOT.transformBy(ROBOT_TO_CAMERA), TAG1));

        Optional<LumenEstimatedRobotPose> pose = estimator(LumenPoseStrategy.CLOSEST_TO_CAMERA_HEIGHT).update(result(Optional.empty(), t));

        assertPose(ROBOT, pose.orElseThrow().getEstimatedPose(), 1e-6);
    }

    @Test
    void closestToLastPoseStartsFromLowestAmbiguityThenFollowsThePreviousEstimate() {
        LumenTrackedTarget first = target(1, TAG1, ROBOT, 0.1, 1.0, mirroredAlternate(TAG1));
        LumenPoseEstimator estimator = estimator(LumenPoseStrategy.CLOSEST_TO_LAST_POSE);

        assertPose(ROBOT, estimator.update(result(Optional.empty(), first)).orElseThrow().getEstimatedPose(), 1e-6);

        // next frame the best hypothesis is the wrong one; staying near the last pose picks the alternate
        LumenTrackedTarget second = target(1, TAG1, new Pose3d(2.0, 0.5, 0.4, new Rotation3d(0.1, 0.2, 0.9)), 0.1, 1.0,
                new Transform3d(ROBOT.transformBy(ROBOT_TO_CAMERA), TAG1));
        assertPose(ROBOT, estimator.update(result(Optional.empty(), second)).orElseThrow().getEstimatedPose(), 1e-6);
    }

    @Test
    void averageBestTargetsBlendsTheVisibleTagsByAmbiguity() {
        Pose3d robotA = new Pose3d(3.0, 1.0, 0.0, new Rotation3d(0, 0, 0.2));
        Pose3d robotB = new Pose3d(3.2, 1.2, 0.0, new Rotation3d(0, 0, 0.4));
        // equal ambiguity -> the midpoint
        LumenTrackedTarget a = target(1, TAG1, robotA, 0.1, 1.0, new Transform3d());
        LumenTrackedTarget b = target(2, TAG2, robotB, 0.1, 1.0, new Transform3d());

        Pose3d average = estimator(LumenPoseStrategy.AVERAGE_BEST_TARGETS).update(result(Optional.empty(), a, b)).orElseThrow().getEstimatedPose();

        assertEquals(3.1, average.getX(), 1e-6);
        assertEquals(1.1, average.getY(), 1e-6);
        assertEquals(0.3, average.getRotation().getZ(), 1e-3);

        // the far more certain tag dominates
        LumenTrackedTarget certain = target(1, TAG1, robotA, 0.001, 1.0, new Transform3d());
        LumenTrackedTarget vague = target(2, TAG2, robotB, 0.5, 1.0, new Transform3d());
        Pose3d weighted = estimator(LumenPoseStrategy.AVERAGE_BEST_TARGETS).update(result(Optional.empty(), certain, vague)).orElseThrow().getEstimatedPose();
        assertEquals(3.0, weighted.getX(), 0.01);
    }

    @Test
    void coprocessorMultiTagComposesTheCameraPoseWithTheMountOffset() {
        Pose3d cameraPose = ROBOT.transformBy(ROBOT_TO_CAMERA);
        LumenMultiTagResult multiTag = new LumenMultiTagResult(cameraPose, List.of(1, 2), 0.4);
        LumenTrackedTarget t1 = target(1, TAG1, ROBOT, 0.1, 1.0, new Transform3d());
        LumenTrackedTarget t3 = target(2, TAG2, ROBOT, 0.1, 1.0, new Transform3d());

        Optional<LumenEstimatedRobotPose> pose = estimator(LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR)
                .update(result(Optional.of(multiTag), t1, t3));

        assertPose(ROBOT, pose.orElseThrow().getEstimatedPose(), 1e-6);
        assertEquals(2, pose.get().getTargetsUsed().size());
        assertEquals(LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR, pose.get().getStrategy());
    }

    @Test
    void multiTagFallsBackWhenThereIsNoCoprocessorSolve() {
        LumenTrackedTarget t = target(1, TAG1, ROBOT, 0.1, 1.0, new Transform3d());
        LumenPoseEstimator estimator = estimator(LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR);

        Optional<LumenEstimatedRobotPose> pose = estimator.update(result(Optional.empty(), t));

        assertPose(ROBOT, pose.orElseThrow().getEstimatedPose(), 1e-6);
        assertEquals(LumenPoseStrategy.LOWEST_AMBIGUITY, pose.get().getStrategy());

        estimator.setMultiTagFallbackStrategy(LumenPoseStrategy.AVERAGE_BEST_TARGETS);
        assertEquals(LumenPoseStrategy.AVERAGE_BEST_TARGETS, estimator.update(result(Optional.empty(), t)).orElseThrow().getStrategy());
    }

    @Test
    void multiTagIsExpressedRelativeToTheLayoutOrigin() {
        // a red-origin layout: the coprocessor's solve is in the blue-origin frame, the robot pose in the red one
        LAYOUT.setOrigin(AprilTagFieldLayout.OriginPosition.kRedAllianceWallRightSide);
        try {
            Pose3d blueCamera = ROBOT.transformBy(ROBOT_TO_CAMERA);
            LumenMultiTagResult multiTag = new LumenMultiTagResult(blueCamera, List.of(1, 2), 0.4);

            Pose3d expected = blueCamera.relativeTo(LAYOUT.getOrigin()).transformBy(ROBOT_TO_CAMERA.inverse());
            Pose3d actual = estimator(LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR)
                    .update(result(Optional.of(multiTag))).orElseThrow().getEstimatedPose();

            assertPose(expected, actual, 1e-9);
        } finally {
            LAYOUT.setOrigin(AprilTagFieldLayout.OriginPosition.kBlueAllianceWallRightSide);
        }
    }

    @Test
    void constrainedSolveReturnsTheCoprocessorsRobotPoseAndFallsBackWithoutOne() {
        LumenConstrainedResult constrained = new LumenConstrainedResult(ROBOT.toPose2d(), 0.3, 1);
        LumenTrackedTarget t = target(1, TAG1, ROBOT, 0.1, 1.0, new Transform3d());
        LumenPipelineResult withConstrained = new LumenPipelineResult(new ArrayList<>(List.of(t)), 12.0, 7, 15000,
                Optional.empty(), Optional.of(constrained));

        LumenPoseEstimator estimator = estimator(LumenPoseStrategy.CONSTRAINED_SOLVEPNP);
        LumenEstimatedRobotPose estimate = estimator.update(withConstrained).orElseThrow();
        assertEquals(LumenPoseStrategy.CONSTRAINED_SOLVEPNP, estimate.getStrategy());
        assertEquals(ROBOT.getX(), estimate.getEstimatedPose().getX(), 1e-9);
        assertEquals(ROBOT.getY(), estimate.getEstimatedPose().getY(), 1e-9);
        assertEquals(0.0, estimate.getEstimatedPose().getZ(), 1e-9);
        assertEquals(0.3, estimate.getEstimatedPose().getRotation().getZ(), 1e-9);

        // no constrained result (no seed published yet): falls back to the lowest-ambiguity strategy
        LumenEstimatedRobotPose fallback = estimator.update(result(Optional.empty(), t)).orElseThrow();
        assertEquals(LumenPoseStrategy.LOWEST_AMBIGUITY, fallback.getStrategy());
    }

    @Test
    void distanceTrigSolveUsesTheHeadingAndTheBestTagsRangeAndBearing() {
        LumenTrackedTarget t = target(1, TAG1, ROBOT, 0.1, 1.0, new Transform3d());
        LumenPoseEstimator estimator = estimator(LumenPoseStrategy.PNP_DISTANCE_TRIG_SOLVE);

        // no heading data yet
        assertTrue(estimator.update(result(Optional.empty(), t)).isEmpty());

        estimator.addHeadingData(11.9, Rotation2d.fromRadians(0.3));
        estimator.addHeadingData(12.1, Rotation2d.fromRadians(0.3));
        Pose3d pose = estimator.update(result(Optional.empty(), t)).orElseThrow().getEstimatedPose();

        // x, y and heading come out right; z is assumed to be the floor
        assertEquals(ROBOT.getX(), pose.getX(), 1e-6);
        assertEquals(ROBOT.getY(), pose.getY(), 1e-6);
        assertEquals(0.0, pose.getZ(), 1e-9);
        assertEquals(0.3, pose.getRotation().getZ(), 1e-9);
    }

    @Test
    void theMultiTagOnlyStrategyWorksWithoutALayout() {
        Pose3d cameraPose = ROBOT.transformBy(ROBOT_TO_CAMERA);
        LumenPoseEstimator noLayout = new LumenPoseEstimator(null, LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR, ROBOT_TO_CAMERA);

        Pose3d pose = noLayout.update(result(Optional.of(new LumenMultiTagResult(cameraPose, List.of(1, 2), 0.3)))).orElseThrow().getEstimatedPose();

        assertPose(ROBOT, pose, 1e-9);
    }
}
