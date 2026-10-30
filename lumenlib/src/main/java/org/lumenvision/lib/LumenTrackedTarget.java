package org.lumenvision.lib;

import edu.wpi.first.math.geometry.Transform3d;

/**
 * One detection as the result packet carries it, in the shape photonlib's PhotonTrackedTarget has (same member names), which the
 * {@code photoncompat} layer is built on. A target is either an AprilTag ({@link #getFiducialId()} &gt;= 0) or an object detection
 * ({@link #getDetectedObjectClassID()} &gt;= 0), so half its fields mean nothing for any one target: robot code reads
 * {@link LumenAprilTagTarget} from a {@link LumenAprilTagDetector} and {@link LumenDetectedObject} from a {@link LumenObjectDetector}.
 *
 * <p>Transforms are camera-to-target in WPILib frames: the camera frame is X forward, Y left, Z up and the target frame is the
 * one a field-layout tag pose describes (X out of the tag's face), so {@code tagFieldPose.transformBy(cameraToTarget.inverse())}
 * is the camera's field pose.
 */
public class LumenTrackedTarget extends LumenTarget {
    private final int fiducialId;
    private final int objectClassId;
    private final float objectConfidence;
    private final double poseAmbiguity;
    private final Transform3d bestCameraToTarget;
    private final Transform3d alternateCameraToTarget;
    private final float bestReprojError;
    private final float altReprojError;

    /** A target with no capture time ({@link #getTimestampSeconds()} 0), e.g. for a test. */
    public LumenTrackedTarget(int fiducialId, int objectClassId, float objectConfidence, double yaw, double pitch, double area,
            double skew, double poseAmbiguity, Transform3d bestCameraToTarget, Transform3d alternateCameraToTarget,
            float bestReprojError, float altReprojError, double[] corners, double[] minAreaRectCorners) {
        this(fiducialId, objectClassId, objectConfidence, yaw, pitch, area, skew, poseAmbiguity, bestCameraToTarget, alternateCameraToTarget,
                bestReprojError, altReprojError, corners, minAreaRectCorners, 0.0, 0);
    }

    /**
     * @param timestampSeconds the frame's capture time (see {@link #getTimestampSeconds()})
     * @param latencyMicros its capture-to-publish latency
     */
    public LumenTrackedTarget(int fiducialId, int objectClassId, float objectConfidence, double yaw, double pitch, double area,
            double skew, double poseAmbiguity, Transform3d bestCameraToTarget, Transform3d alternateCameraToTarget,
            float bestReprojError, float altReprojError, double[] corners, double[] minAreaRectCorners, double timestampSeconds,
            long latencyMicros) {
        super(yaw, pitch, area, skew, corners, minAreaRectCorners, timestampSeconds, latencyMicros);
        this.fiducialId = fiducialId;
        this.objectClassId = objectClassId;
        this.objectConfidence = objectConfidence;
        this.poseAmbiguity = poseAmbiguity;
        this.bestCameraToTarget = bestCameraToTarget;
        this.alternateCameraToTarget = alternateCameraToTarget;
        this.bestReprojError = bestReprojError;
        this.altReprojError = altReprojError;
    }

    /** The decoded AprilTag ID, or -1 for an object detection. */
    public int getFiducialId() {
        return fiducialId;
    }

    /** The detected object's class id, or -1 for an AprilTag. */
    public int getDetectedObjectClassID() {
        return objectClassId;
    }

    /** The detected object's confidence (0 to 1), or -1 for an AprilTag. */
    public float getDetectedObjectConfidence() {
        return objectConfidence;
    }

    /** 0 (unambiguous) to 1 (two equally good pose hypotheses); -1 when no pose was solved. */
    public double getPoseAmbiguity() {
        return poseAmbiguity;
    }

    /** The lower-error camera-to-target pose hypothesis; identity when no pose was solved (no camera calibration). */
    public Transform3d getBestCameraToTarget() {
        return bestCameraToTarget;
    }

    /** The other planar-PnP hypothesis; identity when there was only one or no pose. */
    public Transform3d getAlternateCameraToTarget() {
        return alternateCameraToTarget;
    }

    /** RMS reprojection error in pixels of the best pose; -1 when no pose was solved. */
    public double getBestReprojError() {
        return bestReprojError;
    }

    /** RMS reprojection error in pixels of the alternate pose; -1 when there is none. */
    public double getAltReprojError() {
        return altReprojError;
    }
}
