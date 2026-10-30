package org.lumenvision.lib;

import java.util.List;
import java.util.Optional;

/**
 * One result packet as decoded, in the shape photonlib's PhotonPipelineResult has, which the {@code photoncompat} layer is built on. Its
 * targets combine the AprilTag and object-detection fields (see {@link LumenTrackedTarget}); robot code reads {@link LumenAprilTagResult}
 * or {@link LumenObjectDetectionResult} instead.
 */
public class LumenPipelineResult extends LumenResult<LumenTrackedTarget> {
    private final Optional<LumenMultiTagResult> multiTagResult;
    private final Optional<LumenConstrainedResult> constrainedResult;

    public LumenPipelineResult(List<LumenTrackedTarget> targets, double timestampSeconds, long sequenceId, long latencyMicros,
            Optional<LumenMultiTagResult> multiTagResult) {
        this(targets, timestampSeconds, sequenceId, latencyMicros, multiTagResult, Optional.empty());
    }

    public LumenPipelineResult(List<LumenTrackedTarget> targets, double timestampSeconds, long sequenceId, long latencyMicros,
            Optional<LumenMultiTagResult> multiTagResult, Optional<LumenConstrainedResult> constrainedResult) {
        super(targets, timestampSeconds, sequenceId, latencyMicros);
        this.multiTagResult = multiTagResult;
        this.constrainedResult = constrainedResult;
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
