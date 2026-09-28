package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;

import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform2d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation2d;
import edu.wpi.first.math.geometry.Translation3d;
import org.junit.jupiter.api.Test;

// Hand-verified geometry cases; each formula is checked against an independently derived answer
class LumenUtilsTest {

    private static final double EPS = 1e-9;

    @Test
    void estimateFieldToRobotAprilTag_directlyAheadNoRotation() {
        // tag at field (5,0,0), seen 3m straight ahead with no rotation or mount offset: camera at x = 2
        Pose3d fieldToTarget = new Pose3d(new Translation3d(5, 0, 0), new Rotation3d());
        Transform3d cameraToTarget = new Transform3d(new Translation3d(3, 0, 0), new Rotation3d());
        Transform3d cameraToRobot = new Transform3d();

        Pose3d fieldToRobot = LumenUtils.estimateFieldToRobotAprilTag(cameraToTarget, fieldToTarget, cameraToRobot);

        assertEquals(2.0, fieldToRobot.getX(), EPS);
        assertEquals(0.0, fieldToRobot.getY(), EPS);
        assertEquals(0.0, fieldToRobot.getZ(), EPS);
    }

    @Test
    void estimateFieldToRobotAprilTag_withCameraMountOffset() {
        // as above with the camera mounted 0.5m forward of the robot origin: robot 0.5m behind the camera
        Pose3d fieldToTarget = new Pose3d(new Translation3d(5, 0, 0), new Rotation3d());
        Transform3d cameraToTarget = new Transform3d(new Translation3d(3, 0, 0), new Rotation3d());
        Transform3d cameraToRobot = new Transform3d(new Translation3d(-0.5, 0, 0), new Rotation3d());

        Pose3d fieldToRobot = LumenUtils.estimateFieldToRobotAprilTag(cameraToTarget, fieldToTarget, cameraToRobot);

        assertEquals(1.5, fieldToRobot.getX(), EPS);
    }

    @Test
    void estimateCameraToTarget_directlyAheadNoRotation() {
        // target dead ahead at distance 5, facing -X (180deg), gyro 0deg: translation is unchanged and
        // rotation is 180 - 0 = 180
        Translation2d cameraToTargetTranslation = new Translation2d(5, 0);
        Pose2d targetPose = new Pose2d(10, 0, Rotation2d.k180deg);
        Rotation2d gyroAngle = Rotation2d.kZero;

        Transform2d cameraToTarget = LumenUtils.estimateCameraToTarget(cameraToTargetTranslation, targetPose, gyroAngle);

        assertEquals(5.0, cameraToTarget.getX(), EPS);
        assertEquals(0.0, cameraToTarget.getY(), EPS);
        assertEquals(Math.PI, Math.abs(cameraToTarget.getRotation().getRadians()), EPS);
    }

    @Test
    void estimateFieldToRobot_2d_matchesHandDerivedPosition() {
        // chained with estimateFieldToRobot: camera and robot land at field x = 10 - 5 = 5, facing 0deg
        Translation2d cameraToTargetTranslation = new Translation2d(5, 0);
        Pose2d targetPose = new Pose2d(10, 0, Rotation2d.k180deg);
        Rotation2d gyroAngle = Rotation2d.kZero;
        Transform2d cameraToTarget = LumenUtils.estimateCameraToTarget(cameraToTargetTranslation, targetPose, gyroAngle);

        Pose2d fieldToRobot = LumenUtils.estimateFieldToRobot(cameraToTarget, targetPose, new Transform2d());

        assertEquals(5.0, fieldToRobot.getX(), EPS);
        assertEquals(0.0, fieldToRobot.getY(), EPS);
        assertEquals(0.0, fieldToRobot.getRotation().getRadians(), EPS);
    }

    @Test
    void getYawToPose_isFieldRelativeIndependentOfRobotHeading() {
        // target at (5,5) from a robot at the origin: bearing is 45deg regardless of robot heading (two headings checked)
        Pose2d targetPose = new Pose2d(5, 5, Rotation2d.kZero);

        Rotation2d bearingFacingZero =
                LumenUtils.getYawToPose(new Pose2d(0, 0, Rotation2d.kZero), targetPose);
        Rotation2d bearingFacingNinety =
                LumenUtils.getYawToPose(new Pose2d(0, 0, Rotation2d.kCCW_Pi_2), targetPose);

        assertEquals(Math.PI / 4, bearingFacingZero.getRadians(), EPS);
        assertEquals(Math.PI / 4, bearingFacingNinety.getRadians(), EPS);
    }

    @Test
    void calculateDistanceToTargetMeters_matchesKnownTriangle() {
        // 0.5m camera, 2.5m target, 0deg camera pitch, 45deg measured pitch: horizontal distance equals the height difference
        double distance = LumenUtils.calculateDistanceToTargetMeters(0.5, 2.5, 0.0, Math.PI / 4);
        assertEquals(2.0, distance, EPS);
    }
}
