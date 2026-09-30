package org.lumenvision.photoncompat.sim;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import edu.wpi.first.apriltag.AprilTag;
import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.NetworkTablesJNI;
import java.util.List;
import java.util.Optional;
import java.util.function.BooleanSupplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.lumenvision.photoncompat.LumenCamera;
import org.lumenvision.photoncompat.LumenEstimatedRobotPose;
import org.lumenvision.photoncompat.LumenPipelineResult;
import org.lumenvision.photoncompat.LumenPoseEstimator;
import org.lumenvision.photoncompat.LumenPoseStrategy;
import org.lumenvision.photoncompat.LumenTrackedTarget;

// The simulated camera and a LumenCamera share one NT instance, as they do in a robot simulation, so what the robot code reads is
// exactly what the simulation published.
class VisionSystemSimTest {
    private static final String ROOT = "lumenvision";
    private static final Transform3d ROBOT_TO_CAMERA = new Transform3d(new Translation3d(0.3, 0.0, 0.5), new Rotation3d(0, -0.1, 0));
    // tag 1 faces -X on the far wall; tag 2 is beside it, angled towards the robot
    private static final Pose3d TAG1 = new Pose3d(6.0, 1.0, 0.8, new Rotation3d(0, 0, Math.PI));
    private static final Pose3d TAG2 = new Pose3d(6.0, 1.8, 0.8, new Rotation3d(0, 0, Math.PI - 0.3));
    private static final AprilTagFieldLayout LAYOUT = new AprilTagFieldLayout(
            List.of(new AprilTag(1, TAG1), new AprilTag(2, TAG2)), 16.0, 8.0);

    private NetworkTableInstance nt;
    private LumenCamera camera;
    private PhotonCameraSim cameraSim;
    private VisionSystemSim system;

    @BeforeEach
    void start() {
        nt = NetworkTableInstance.create();
        nt.startServer("", "127.0.0.1", 18819, 18820);
        LumenCamera.setVersionCheckEnabled(false);
        camera = new LumenCamera(nt, ROOT, "front");
        cameraSim = new PhotonCameraSim(camera, new SimCameraProperties().setFPS(1000));
        system = new VisionSystemSim("test");
        system.addCamera(cameraSim, ROBOT_TO_CAMERA);
    }

    @AfterEach
    void stop() {
        cameraSim.close();
        camera.close();
        nt.stopServer();
        nt.close();
    }

    private static boolean waitFor(BooleanSupplier condition) {
        long deadline = System.currentTimeMillis() + 5000;
        while (System.currentTimeMillis() < deadline) {
            if (condition.getAsBoolean()) return true;
            try {
                Thread.sleep(5);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        return condition.getAsBoolean();
    }

    // runs one simulation step and returns the result it produced
    private LumenPipelineResult step(Pose2d robot) {
        long before = camera.getLatestResult().getSequenceId();
        try {
            Thread.sleep(3); // longer than the 1 ms frame interval
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        system.update(robot);
        assertTrue(waitFor(() -> camera.getLatestResult().getSequenceId() > before));
        return camera.getLatestResult();
    }

    @Test
    void aTagStraightAheadIsReportedWithTheGroundTruthTransform() {
        system.addAprilTags(LAYOUT);
        Pose2d robot = new Pose2d(3.0, 1.0, new Rotation2d());
        LumenPipelineResult result = step(robot);

        LumenTrackedTarget target = result.getTargets().stream().filter(t -> t.getFiducialId() == 1).findFirst().orElseThrow();
        Pose3d cameraPose = new Pose3d(robot).transformBy(ROBOT_TO_CAMERA);
        Transform3d expected = new Transform3d(cameraPose, TAG1);
        assertEquals(expected.getX(), target.getBestCameraToTarget().getX(), 1e-4);
        assertEquals(expected.getY(), target.getBestCameraToTarget().getY(), 1e-4);
        assertEquals(expected.getZ(), target.getBestCameraToTarget().getZ(), 1e-4);
        assertEquals(0.0, expected.getRotation().minus(target.getBestCameraToTarget().getRotation()).getAngle(), 1e-4);
        assertEquals(0.0, target.getPoseAmbiguity(), 1e-9);
        assertTrue(target.getArea() > 0);
        assertEquals(4, target.getDetectedCorners().size());
    }

    @Test
    void yawIsPositiveToTheLeftAndPitchPositiveUp() {
        system.addVisionTargets(new VisionTargetSim(new Pose3d(6.0, 2.0, 2.0, new Rotation3d(0, 0, Math.PI)), TargetModel.kAprilTag36h11, 9));
        LumenPipelineResult result = step(new Pose2d(3.0, 1.0, new Rotation2d()));

        LumenTrackedTarget target = result.getBestTarget().orElseThrow();
        assertTrue(target.getYaw() > 0, "a tag to the left has positive yaw");
        assertTrue(target.getPitch() > 0, "a tag above the camera has positive pitch");
    }

    @Test
    void aTagSeenFromBehindOrOutsideTheViewIsNotDetected() {
        system.addVisionTargets(new VisionTargetSim(new Pose3d(6.0, 1.0, 0.8, new Rotation3d()), TargetModel.kAprilTag36h11, 1)); // faces away
        assertFalse(step(new Pose2d(3.0, 1.0, new Rotation2d())).hasTargets());

        system.clearVisionTargets();
        system.addVisionTargets(new VisionTargetSim(TAG1, TargetModel.kAprilTag36h11, 1)); // in front, but the robot faces away from it
        assertFalse(step(new Pose2d(3.0, 1.0, Rotation2d.fromDegrees(180))).hasTargets());
    }

    @Test
    void twoTagsGiveAMultiTagResultTheEstimatorTurnsBackIntoTheRobotPose() {
        system.addAprilTags(LAYOUT);
        Pose2d robot = new Pose2d(3.0, 1.2, Rotation2d.fromDegrees(10));
        LumenPipelineResult result = step(robot);
        assertTrue(result.getMultiTagResult().isPresent());
        assertEquals(List.of(1, 2), result.getMultiTagResult().get().getFiducialIds().stream().sorted().toList());

        LumenPoseEstimator estimator = new LumenPoseEstimator(LAYOUT, LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR, camera, ROBOT_TO_CAMERA);
        LumenEstimatedRobotPose estimate = estimator.update().orElseThrow();
        assertEquals(LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR, estimate.getStrategy());
        assertEquals(robot.getX(), estimate.getEstimatedPose().getX(), 1e-6);
        assertEquals(robot.getY(), estimate.getEstimatedPose().getY(), 1e-6);
    }

    @Test
    void theMultiTagPoseFollowsTheLayoutOrigin() {
        LAYOUT.setOrigin(AprilTagFieldLayout.OriginPosition.kRedAllianceWallRightSide);
        try {
            system.addAprilTags(LAYOUT);
            // the robot stands 3 m in front of tag 1 (in the red-origin frame the layout now reports), facing it
            Pose3d tag = LAYOUT.getTagPose(1).orElseThrow();
            Pose3d front = tag.transformBy(new Transform3d(new Translation3d(3.0, 0, 0), new Rotation3d(0, 0, Math.PI)));
            Pose2d placed = new Pose2d(front.getX(), front.getY(), front.toPose2d().getRotation());
            step(placed);

            LumenPoseEstimator estimator = new LumenPoseEstimator(LAYOUT, LumenPoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR, camera, ROBOT_TO_CAMERA);
            Optional<LumenEstimatedRobotPose> estimate = estimator.update();
            assertTrue(estimate.isPresent());
            assertEquals(placed.getX(), estimate.get().getEstimatedPose().getX(), 1e-6);
            assertEquals(placed.getY(), estimate.get().getEstimatedPose().getY(), 1e-6);
            assertEquals(0.0, estimate.get().getEstimatedPose().getZ(), 1e-6);
        } finally {
            LAYOUT.setOrigin(AprilTagFieldLayout.OriginPosition.kBlueAllianceWallRightSide);
        }
    }

    @Test
    void theConstrainedSolveRunsOnceTheEstimatorPublishesItsSeed() {
        system.addAprilTags(LAYOUT);
        Pose2d robot = new Pose2d(3.0, 1.2, Rotation2d.fromDegrees(10));
        LumenPoseEstimator estimator = new LumenPoseEstimator(LAYOUT, LumenPoseStrategy.CONSTRAINED_SOLVEPNP, camera, ROBOT_TO_CAMERA);
        estimator.setReferencePose(new Pose2d(2.5, 1.0, new Rotation2d()));

        LumenEstimatedRobotPose[] constrained = new LumenEstimatedRobotPose[1];
        assertTrue(waitFor(() -> {
            estimator.update(); // publishes the seed and mount each call
            step(robot);
            Optional<LumenEstimatedRobotPose> estimate = estimator.estimateConstrainedSolvePnpPose(camera.getLatestResult());
            estimate.ifPresent(e -> constrained[0] = e);
            return estimate.isPresent();
        }));
        assertEquals(robot.getX(), constrained[0].getEstimatedPose().getX(), 1e-6);
        assertEquals(robot.getY(), constrained[0].getEstimatedPose().getY(), 1e-6);
        assertEquals(robot.getRotation().getRadians(), constrained[0].getEstimatedPose().getRotation().getZ(), 1e-6);
    }

    @Test
    void theFrameIntervalAndFpsLimitGateHowOftenResultsAreProduced() {
        PhotonCameraSim slow = new PhotonCameraSim(camera, new SimCameraProperties().setFPS(10));
        try {
            long now = NetworkTablesJNI.now();
            assertTrue(slow.update(now, new Pose3d(), List.of()));
            assertFalse(slow.update(now + 50_000, new Pose3d(), List.of()));
            assertTrue(slow.update(now + 100_000, new Pose3d(), List.of()));
        } finally {
            slow.close();
        }
    }

    @Test
    void resultsAreStampedWithTheCaptureTimeAndCarryTheLatency() {
        PhotonCameraSim laggy = new PhotonCameraSim(camera, new SimCameraProperties().setAvgLatencyMs(80));
        try {
            long before = camera.getLatestResult().getSequenceId();
            long now = NetworkTablesJNI.now();
            assertTrue(laggy.update(now, new Pose3d(), List.of()));
            assertTrue(waitFor(() -> camera.getLatestResult().getSequenceId() > before));

            LumenPipelineResult result = camera.getLatestResult();
            assertEquals(80.0, result.getLatencyMillis(), 1e-6);
            assertEquals((now - 80_000) / 1e6, result.getTimestampSeconds(), 1e-3);
        } finally {
            laggy.close();
        }
    }

    @Test
    void cornerNoiseMovesTheReportedCornersOnly() {
        PhotonCameraSim noisy = new PhotonCameraSim(camera, new SimCameraProperties().setCalibError(2.0, 0.0));
        noisy.setRandomSeed(1);
        try {
            long before = camera.getLatestResult().getSequenceId();
            assertTrue(noisy.update(NetworkTablesJNI.now(), new Pose3d(3.0, 1.0, 0.8, new Rotation3d()), List.of(new VisionTargetSim(TAG1, TargetModel.kAprilTag36h11, 1))));
            assertTrue(waitFor(() -> camera.getLatestResult().getSequenceId() > before));

            LumenTrackedTarget target = camera.getLatestResult().getBestTarget().orElseThrow();
            assertEquals(3.0, target.getBestCameraToTarget().getX(), 1e-4);
        } finally {
            noisy.close();
        }
    }

    @Test
    void theMinAreaRectOfAnAxisAlignedSquareIsThatSquare() {
        double[] rect = new double[8];
        double angle = PhotonCameraSim.minAreaRect(new double[] { 0, 10, 10, 10, 10, 0, 0, 0 }, rect);
        assertEquals(0.0, angle, 1e-9);
        double minX = Double.MAX_VALUE, maxX = -Double.MAX_VALUE;
        for (int i = 0; i < 4; i++) {
            minX = Math.min(minX, rect[i * 2]);
            maxX = Math.max(maxX, rect[i * 2]);
        }
        assertEquals(10.0, maxX - minX, 1e-9);
    }
}
