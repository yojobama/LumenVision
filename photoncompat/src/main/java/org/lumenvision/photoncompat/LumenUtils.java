package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Transform2d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation2d;

/**
 * Static helpers for turning a {@link LumenTrackedTarget} into robot/field poses, mirroring
 * photonlib's {@code PhotonUtils}.
 *
 * <p>{@link #estimateFieldToRobotAprilTag} uses the full 3D {@link
 * LumenTrackedTarget#getBestCameraToTarget()}; the 2D pitch/yaw helpers assume known target height
 * and camera mount angle.
 */
public final class LumenUtils {
    private LumenUtils() {}

    /**
     * Distance from the camera to a target from mount geometry and the target's pitch (the "known
     * target height" triangle). Prefer {@link #estimateFieldToRobotAprilTag} when a 3D {@link
     * Transform3d} is available.
     *
     * @param cameraHeightMeters height of the camera's lens off the floor
     * @param targetHeightMeters height of the target off the floor
     * @param cameraPitchRadians camera mount angle above horizontal (positive = tilted up)
     * @param targetPitchRadians the target's measured pitch in the camera's image (positive = up)
     */
    public static double calculateDistanceToTargetMeters(
            double cameraHeightMeters,
            double targetHeightMeters,
            double cameraPitchRadians,
            double targetPitchRadians) {
        return (targetHeightMeters - cameraHeightMeters)
                / Math.tan(cameraPitchRadians + targetPitchRadians);
    }

    /** The camera-to-target translation implied by a known distance and measured yaw. */
    public static Translation2d estimateCameraToTargetTranslation(
            double targetDistanceMeters, Rotation2d yaw) {
        return new Translation2d(targetDistanceMeters, yaw);
    }

    /**
     * Composes a camera-to-target translation with the target's field pose and the gyro heading
     * into a camera-to-target {@link Transform2d} (2D only: no roll/pitch, no camera mount transform).
     *
     * <p>The translation is already in the camera frame; the rotation is the target's field rotation
     * minus the camera's field heading (gyroAngle).
     */
    public static Transform2d estimateCameraToTarget(
            Translation2d cameraToTargetTranslation, Pose2d targetPose, Rotation2d gyroAngle) {
        return new Transform2d(cameraToTargetTranslation, targetPose.getRotation().minus(gyroAngle));
    }

    /**
     * The field-relative robot pose from one AprilTag detection's 3D pose: {@code cameraToTarget}
     * is {@link LumenTrackedTarget#getBestCameraToTarget()}, {@code fieldToTarget} is the tag's
     * field pose and {@code cameraToRobot} is the camera mount offset. A single tag is noisier
     * than a multi-tag solve at range or oblique angles.
     */
    public static Pose3d estimateFieldToRobotAprilTag(
            Transform3d cameraToTarget, Pose3d fieldToTarget, Transform3d cameraToRobot) {
        return fieldToTarget.transformBy(cameraToTarget.inverse()).transformBy(cameraToRobot);
    }

    /** The 2D robot pose from a camera-to-target transform, the target's field pose and the camera mount offset. */
    public static Pose2d estimateFieldToRobot(
            Transform2d cameraToTarget, Pose2d fieldToTarget, Transform2d cameraToRobot) {
        return fieldToTarget.transformBy(cameraToTarget.inverse()).transformBy(cameraToRobot);
    }

    /**
     * The 2D robot pose from mount geometry and a target measurement: the distance from the target's pitch (see {@link
     * #calculateDistanceToTargetMeters}), its yaw, the gyro heading and the target's field pose.
     */
    public static Pose2d estimateFieldToRobot(
            double cameraHeightMeters,
            double targetHeightMeters,
            double cameraPitchRadians,
            double targetPitchRadians,
            Rotation2d targetYaw,
            Rotation2d gyroAngle,
            Pose2d fieldToTarget,
            Transform2d cameraToRobot) {
        return estimateFieldToRobot(
                estimateCameraToTarget(
                        estimateCameraToTargetTranslation(
                                calculateDistanceToTargetMeters(
                                        cameraHeightMeters, targetHeightMeters, cameraPitchRadians, targetPitchRadians),
                                targetYaw),
                        fieldToTarget,
                        gyroAngle),
                fieldToTarget,
                cameraToRobot);
    }

    /** The straight-line distance between the robot's pose and a target's field pose, in metres. */
    public static double getDistanceToPose(Pose2d robotPose, Pose2d targetPose) {
        return robotPose.getTranslation().getDistance(targetPose.getTranslation());
    }

    /** The bearing from the robot's pose to a target's field pose, e.g. for a turn-to-face setpoint. */
    public static Rotation2d getYawToPose(Pose2d robotPose, Pose2d targetPose) {
        Translation2d relativeTrl = targetPose.relativeTo(robotPose).getTranslation();
        return new Rotation2d(relativeTrl.getX(), relativeTrl.getY()).plus(robotPose.getRotation());
    }
}
