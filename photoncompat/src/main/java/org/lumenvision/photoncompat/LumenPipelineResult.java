package org.lumenvision.photoncompat;

import java.util.Collections;
import java.util.List;
import java.util.Optional;

/**
 * One snapshot of a coprocessor node's detections, in the shape photonlib's
 * PhotonPipelineResult already has.
 */
public class LumenPipelineResult {
    private final List<LumenTrackedTarget> targets;
    private final double timestampSeconds;
    private final Optional<LumenMultiTagResult> multiTagResult;

    LumenPipelineResult(
            List<LumenTrackedTarget> targets,
            double timestampSeconds,
            Optional<LumenMultiTagResult> multiTagResult) {
        this.targets = Collections.unmodifiableList(targets);
        this.timestampSeconds = timestampSeconds;
        this.multiTagResult = multiTagResult;
    }

    public List<LumenTrackedTarget> getTargets() {
        return targets;
    }

    public boolean hasTargets() {
        return !targets.isEmpty();
    }

    /**
     * The moment this result was published, in the local clock domain (the NT4 entry timestamp);
     * feed this to a pose estimator's addVisionMeasurement.
     */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /**
     * The coprocessor's multi-tag PnP result for this snapshot, if published (see {@link
     * LumenCamera#getMultiTagResult()}); read in the same NT4 poll as the targets.
     */
    public Optional<LumenMultiTagResult> getMultiTagResult() {
        return multiTagResult;
    }
}
