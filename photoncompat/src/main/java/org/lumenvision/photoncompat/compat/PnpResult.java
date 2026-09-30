package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.geometry.Transform3d;

/**
 * A multi-tag PnP solution, as photonlib's PnpResult. LumenVision's coprocessor returns a single solution, so {@code alt} equals
 * {@code best} and {@code ambiguity} is 0.
 */
public class PnpResult {
    /** The field-to-camera transform (camera pose in the field, WPILib camera axes). */
    public final Transform3d best;
    public final Transform3d alt;
    public final double bestReprojErr;
    public final double altReprojErr;
    public final double ambiguity;

    public PnpResult(Transform3d best, Transform3d alt, double bestReprojErr, double altReprojErr, double ambiguity) {
        this.best = best;
        this.alt = alt;
        this.bestReprojErr = bestReprojErr;
        this.altReprojErr = altReprojErr;
        this.ambiguity = ambiguity;
    }
}
