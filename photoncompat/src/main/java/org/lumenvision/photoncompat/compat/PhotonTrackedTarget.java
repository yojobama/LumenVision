package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.geometry.Transform3d;
import org.lumenvision.photoncompat.LumenTrackedTarget;

/** Thin {@link LumenTrackedTarget} wrapper for drop-in migration. */
public class PhotonTrackedTarget {
    private final LumenTrackedTarget lumenTarget;

    PhotonTrackedTarget(LumenTrackedTarget lumenTarget) {
        this.lumenTarget = lumenTarget;
    }

    public int getFiducialId() {
        return lumenTarget.getFiducialId();
    }

    public Transform3d getBestCameraToTarget() {
        return lumenTarget.getBestCameraToTarget();
    }

    public LumenTrackedTarget getLumenTrackedTarget() {
        return lumenTarget;
    }
}
