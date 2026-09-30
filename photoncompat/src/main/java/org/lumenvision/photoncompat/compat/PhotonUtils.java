package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Transform2d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation2d;
import org.lumenvision.photoncompat.LumenUtils;

/** Thin {@link LumenUtils} wrapper with the same method names and signatures. */
public final class PhotonUtils {
    private PhotonUtils() {}

    public static double calculateDistanceToTargetMeters(
            double cameraHeightMeters,
            double targetHeightMeters,
            double cameraPitchRadians,
            double targetPitchRadians) {
        return LumenUtils.calculateDistanceToTargetMeters(
                cameraHeightMeters, targetHeightMeters, cameraPitchRadians, targetPitchRadians);
    }

    public static Translation2d estimateCameraToTargetTranslation(
            double targetDistanceMeters, Rotation2d yaw) {
        return LumenUtils.estimateCameraToTargetTranslation(targetDistanceMeters, yaw);
    }

    public static Transform2d estimateCameraToTarget(
            Translation2d cameraToTargetTranslation, Pose2d targetPose, Rotation2d gyroAngle) {
        return LumenUtils.estimateCameraToTarget(cameraToTargetTranslation, targetPose, gyroAngle);
    }

    public static Pose3d estimateFieldToRobotAprilTag(
            Transform3d cameraToTarget, Pose3d fieldToTarget, Transform3d cameraToRobot) {
        return LumenUtils.estimateFieldToRobotAprilTag(cameraToTarget, fieldToTarget, cameraToRobot);
    }

    public static Pose2d estimateFieldToRobot(
            Transform2d cameraToTarget, Pose2d fieldToTarget, Transform2d cameraToRobot) {
        return LumenUtils.estimateFieldToRobot(cameraToTarget, fieldToTarget, cameraToRobot);
    }

    public static Pose2d estimateFieldToRobot(
            double cameraHeightMeters,
            double targetHeightMeters,
            double cameraPitchRadians,
            double targetPitchRadians,
            Rotation2d targetYaw,
            Rotation2d gyroAngle,
            Pose2d fieldToTarget,
            Transform2d cameraToRobot) {
        return LumenUtils.estimateFieldToRobot(cameraHeightMeters, targetHeightMeters, cameraPitchRadians, targetPitchRadians,
                targetYaw, gyroAngle, fieldToTarget, cameraToRobot);
    }

    public static double getDistanceToPose(Pose2d robotPose, Pose2d targetPose) {
        return LumenUtils.getDistanceToPose(robotPose, targetPose);
    }

    public static Rotation2d getYawToPose(Pose2d robotPose, Pose2d targetPose) {
        return LumenUtils.getYawToPose(robotPose, targetPose);
    }
}
