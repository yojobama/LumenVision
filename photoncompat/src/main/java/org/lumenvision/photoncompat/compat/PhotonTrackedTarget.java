package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.geometry.Transform3d;
import org.lumenvision.photoncompat.LumenTargetCorner;
import org.lumenvision.photoncompat.LumenTrackedTarget;

import java.util.List;
import java.util.stream.Collectors;

/** {@link LumenTrackedTarget} with photonlib's PhotonTrackedTarget method names, for drop-in migration. */
public class PhotonTrackedTarget {
    private final LumenTrackedTarget lumenTarget;

    PhotonTrackedTarget(LumenTrackedTarget lumenTarget) {
        this.lumenTarget = lumenTarget;
    }

    /** Horizontal angle to the target in degrees, positive to the left. */
    public double getYaw() {
        return lumenTarget.getYaw();
    }

    /** Vertical angle to the target in degrees, positive up. */
    public double getPitch() {
        return lumenTarget.getPitch();
    }

    /** Percentage of the image the target covers (0 to 100). */
    public double getArea() {
        return lumenTarget.getArea();
    }

    public double getSkew() {
        return lumenTarget.getSkew();
    }

    public List<TargetCorner> getDetectedCorners() {
        return corners(lumenTarget.getDetectedCorners());
    }

    public List<TargetCorner> getMinAreaRectCorners() {
        return corners(lumenTarget.getMinAreaRectCorners());
    }

    public int getFiducialId() {
        return lumenTarget.getFiducialId();
    }

    public int getDetectedObjectClassID() {
        return lumenTarget.getDetectedObjectClassID();
    }

    public float getDetectedObjectConfidence() {
        return lumenTarget.getDetectedObjectConfidence();
    }

    public double getPoseAmbiguity() {
        return lumenTarget.getPoseAmbiguity();
    }

    public Transform3d getBestCameraToTarget() {
        return lumenTarget.getBestCameraToTarget();
    }

    public Transform3d getAlternateCameraToTarget() {
        return lumenTarget.getAlternateCameraToTarget();
    }

    public double getBestReprojError() {
        return lumenTarget.getBestReprojError();
    }

    public double getAltReprojError() {
        return lumenTarget.getAltReprojError();
    }

    public LumenTrackedTarget getLumenTrackedTarget() {
        return lumenTarget;
    }

    private static List<TargetCorner> corners(List<LumenTargetCorner> corners) {
        return corners.stream().map(c -> new TargetCorner(c.x, c.y)).collect(Collectors.toList());
    }
}
