package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Transform3d;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * One detection, in the shape photonlib's PhotonTrackedTarget has (same member names) so a team migrating from PhotonVision
 * changes an import line, not call sites. A target is either an AprilTag ({@link #getFiducialId()} &gt;= 0) or an object
 * detection ({@link #getDetectedObjectClassID()} &gt;= 0).
 *
 * <p>Transforms are camera-to-target in WPILib frames: the camera frame is X forward, Y left, Z up and the target frame is the
 * one a field-layout tag pose describes (X out of the tag's face), so {@code tagFieldPose.transformBy(cameraToTarget.inverse())}
 * is the camera's field pose.
 */
public class LumenTrackedTarget {
    private final int fiducialId;
    private final int objectClassId;
    private final float objectConfidence;
    private final double yaw;
    private final double pitch;
    private final double area;
    private final double skew;
    private final double poseAmbiguity;
    private final Transform3d bestCameraToTarget;
    private final Transform3d alternateCameraToTarget;
    private final float bestReprojError;
    private final float altReprojError;
    private final List<LumenTargetCorner> detectedCorners;
    private final List<LumenTargetCorner> minAreaRectCorners;

    LumenTrackedTarget(int fiducialId, int objectClassId, float objectConfidence, double yaw, double pitch, double area,
            double skew, double poseAmbiguity, Transform3d bestCameraToTarget, Transform3d alternateCameraToTarget,
            float bestReprojError, float altReprojError, double[] corners, double[] minAreaRectCorners) {
        this.fiducialId = fiducialId;
        this.objectClassId = objectClassId;
        this.objectConfidence = objectConfidence;
        this.yaw = yaw;
        this.pitch = pitch;
        this.area = area;
        this.skew = skew;
        this.poseAmbiguity = poseAmbiguity;
        this.bestCameraToTarget = bestCameraToTarget;
        this.alternateCameraToTarget = alternateCameraToTarget;
        this.bestReprojError = bestReprojError;
        this.altReprojError = altReprojError;
        this.detectedCorners = toCorners(corners);
        this.minAreaRectCorners = toCorners(minAreaRectCorners);
    }

    private static List<LumenTargetCorner> toCorners(double[] xy) {
        List<LumenTargetCorner> out = new ArrayList<>(xy.length / 2);
        for (int i = 0; i + 1 < xy.length; i += 2) out.add(new LumenTargetCorner(xy[i], xy[i + 1]));
        return Collections.unmodifiableList(out);
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

    /** Horizontal angle to the target in degrees, positive to the left. With a calibration it is the target's centre; without one 0. */
    public double getYaw() {
        return yaw;
    }

    /** Vertical angle to the target in degrees, positive up. */
    public double getPitch() {
        return pitch;
    }

    /** Percentage (0 to 100) of the image the target's bounding quad covers; 0 without a calibration (the frame size is unknown). */
    public double getArea() {
        return area;
    }

    /** Rotation of the target's minimum-area rectangle in degrees, as OpenCV reports it. */
    public double getSkew() {
        return skew;
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

    /** The detected corners in image pixels (an AprilTag's four, or an object's bounding box). */
    public List<LumenTargetCorner> getDetectedCorners() {
        return detectedCorners;
    }

    /** The minimum-area rectangle around the detected corners, in image pixels. */
    public List<LumenTargetCorner> getMinAreaRectCorners() {
        return minAreaRectCorners;
    }
}
