package org.lumenvision.photoncompat.compat;

import java.util.ArrayList;
import java.util.List;

/** The coprocessor's multi-tag localisation, as photonlib's MultiTargetPNPResult. */
public class MultiTargetPNPResult {
    public final PnpResult estimatedPose;
    public final List<Short> fiducialIDsUsed;

    public MultiTargetPNPResult(PnpResult estimatedPose, List<Short> fiducialIDsUsed) {
        this.estimatedPose = estimatedPose;
        this.fiducialIDsUsed = new ArrayList<>(fiducialIDsUsed);
    }
}
