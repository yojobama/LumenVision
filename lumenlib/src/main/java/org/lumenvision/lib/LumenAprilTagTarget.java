package org.lumenvision.lib;

import edu.wpi.first.math.geometry.Transform3d;

/**
 * An AprilTag seen by a {@link LumenAprilTagDetector}: its id, image position and, on a calibrated camera, its pose relative to the camera.
 *
 * <p>Transforms are camera-to-target in WPILib frames: the camera frame is X forward, Y left, Z up and the target frame is the one a
 * field-layout tag pose describes (X out of the tag's face), so {@code tagFieldPose.transformBy(cameraToTarget.inverse())} is the
 * camera's field pose.
 */
public class LumenAprilTagTarget extends LumenTarget {
    private final int fiducialId;
    private final double poseAmbiguity;
    private final Transform3d bestCameraToTarget;
    private final Transform3d alternateCameraToTarget;
    private final double bestReprojError;
    private final double altReprojError;

    /**
     * @param corners the tag's four corners as x0,y0..x3,y3 in image pixels
     * @param minAreaRectCorners the minimum-area rectangle around them, in the same form
     * @param timestampSeconds the frame's capture time (see {@link #getTimestampSeconds()}); 0 for none, e.g. in a test
     * @param latencyMicros its capture-to-publish latency
     */
    public LumenAprilTagTarget(int fiducialId, double yaw, double pitch, double area, double skew, double poseAmbiguity,
            Transform3d bestCameraToTarget, Transform3d alternateCameraToTarget, double bestReprojError, double altReprojError,
            double[] corners, double[] minAreaRectCorners, double timestampSeconds, long latencyMicros) {
        super(yaw, pitch, area, skew, corners, minAreaRectCorners, timestampSeconds, latencyMicros);
        this.fiducialId = fiducialId;
        this.poseAmbiguity = poseAmbiguity;
        this.bestCameraToTarget = bestCameraToTarget;
        this.alternateCameraToTarget = alternateCameraToTarget;
        this.bestReprojError = bestReprojError;
        this.altReprojError = altReprojError;
    }

    LumenAprilTagTarget(LumenTrackedTarget packet) {
        super(packet);
        this.fiducialId = packet.getFiducialId();
        this.poseAmbiguity = packet.getPoseAmbiguity();
        this.bestCameraToTarget = packet.getBestCameraToTarget();
        this.alternateCameraToTarget = packet.getAlternateCameraToTarget();
        this.bestReprojError = packet.getBestReprojError();
        this.altReprojError = packet.getAltReprojError();
    }

    /** The decoded AprilTag ID. */
    public int getFiducialId() {
        return fiducialId;
    }

    /**
     * Whether the coprocessor solved the tag's pose. It needs the camera's calibration at the running resolution; without one the
     * transforms are identity and the ambiguity and reprojection errors -1.
     */
    public boolean hasPose() {
        return poseAmbiguity >= 0;
    }

    /** 0 (unambiguous) to 1 (two equally good pose hypotheses); -1 when no pose was solved. */
    public double getPoseAmbiguity() {
        return poseAmbiguity;
    }

    /** The lower-error camera-to-target pose hypothesis; identity when no pose was solved (see {@link #hasPose()}). */
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
