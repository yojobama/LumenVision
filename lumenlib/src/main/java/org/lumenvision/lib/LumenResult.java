package org.lumenvision.lib;

import java.util.Collections;
import java.util.List;
import java.util.Optional;

/**
 * One frame's results from a detector node: its targets and when the frame was captured. {@link LumenAprilTagResult} and
 * {@link LumenObjectDetectionResult} are the two kinds; {@link LumenPipelineResult} is the packet's combined form.
 *
 * @param <T> the kind of target
 */
public abstract class LumenResult<T extends LumenTarget> {
    private final List<T> targets;
    private final double timestampSeconds;
    private final long sequenceId;
    private final long latencyMicros;

    protected LumenResult(List<T> targets, double timestampSeconds, long sequenceId, long latencyMicros) {
        this.targets = Collections.unmodifiableList(targets);
        this.timestampSeconds = timestampSeconds;
        this.sequenceId = sequenceId;
        this.latencyMicros = latencyMicros;
    }

    /** Targets, largest image area first. */
    public List<T> getTargets() {
        return targets;
    }

    public boolean hasTargets() {
        return !targets.isEmpty();
    }

    /** The largest target, or empty when there are none. */
    public Optional<T> getBestTarget() {
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

    long latencyMicros() {
        return latencyMicros;
    }

    /** The coprocessor's per-node frame counter, increasing with every published result; 0 before the node has published one. */
    public long getSequenceId() {
        return sequenceId;
    }
}
