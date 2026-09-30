package org.lumenvision.photoncompat;

import java.util.Collections;
import java.util.List;
import java.util.Optional;

/** One snapshot of a coprocessor camera's detections, in the shape photonlib's PhotonPipelineResult has. */
public class LumenPipelineResult {
    private final List<LumenTrackedTarget> targets;
    private final double timestampSeconds;
    private final long sequenceId;
    private final long latencyMicros;
    private final Optional<LumenMultiTagResult> multiTagResult;
    private final Optional<LumenConstrainedResult> constrainedResult;

    LumenPipelineResult(List<LumenTrackedTarget> targets, double timestampSeconds, long sequenceId, long latencyMicros,
            Optional<LumenMultiTagResult> multiTagResult) {
        this(targets, timestampSeconds, sequenceId, latencyMicros, multiTagResult, Optional.empty());
    }

    LumenPipelineResult(List<LumenTrackedTarget> targets, double timestampSeconds, long sequenceId, long latencyMicros,
            Optional<LumenMultiTagResult> multiTagResult, Optional<LumenConstrainedResult> constrainedResult) {
        this.targets = Collections.unmodifiableList(targets);
        this.timestampSeconds = timestampSeconds;
        this.sequenceId = sequenceId;
        this.latencyMicros = latencyMicros;
        this.multiTagResult = multiTagResult;
        this.constrainedResult = constrainedResult;
    }

    /** Targets, largest image area first. */
    public List<LumenTrackedTarget> getTargets() {
        return targets;
    }

    public boolean hasTargets() {
        return !targets.isEmpty();
    }

    /** The largest target, or empty when there are none. */
    public Optional<LumenTrackedTarget> getBestTarget() {
        return targets.isEmpty() ? Optional.empty() : Optional.of(targets.get(0));
    }

    /**
     * When the frame was captured, in seconds on the NetworkTables server clock (the roboRIO's FPGA time): feed this to a pose
     * estimator's addVisionMeasurement. The coprocessor stamps every topic with the capture time, so this is not the arrival time.
     */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /** The frame's capture time in microseconds (same clock as {@link #getTimestampSeconds()}). */
    public long getCaptureTimestampMicros() {
        return Math.round(timestampSeconds * 1_000_000.0);
    }

    /** When the coprocessor published the result, in microseconds (capture time plus {@link #getLatencyMillis()}). */
    public long getPublishTimestampMicros() {
        return getCaptureTimestampMicros() + latencyMicros;
    }

    /** Capture-to-publish latency in milliseconds. */
    public double getLatencyMillis() {
        return latencyMicros / 1000.0;
    }

    /** The coprocessor's per-camera frame counter, increasing with every published result. */
    public long getSequenceId() {
        return sequenceId;
    }

    /** The coprocessor's multi-tag PnP result for this snapshot, if it produced one. */
    public Optional<LumenMultiTagResult> getMultiTagResult() {
        return multiTagResult;
    }

    /** The coprocessor's floor-constrained solve for this snapshot, if the robot has published a seed pose and camera mount and a tag was visible. */
    public Optional<LumenConstrainedResult> getConstrainedResult() {
        return constrainedResult;
    }
}
