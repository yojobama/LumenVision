package org.lumenvision.lib;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/** One frame's results from a {@link LumenAprilTagDetector}: the tags it saw and the coprocessor's pose solves over them. */
public class LumenAprilTagResult extends LumenResult<LumenAprilTagTarget> {
    private final Optional<LumenMultiTagResult> multiTagResult;
    private final Optional<LumenConstrainedResult> constrainedResult;

    /**
     * @param targets the tags, largest image area first
     * @param timestampSeconds the frame's capture time (see {@link #getTimestampSeconds()}); 0 for none, e.g. in a test
     */
    public LumenAprilTagResult(List<LumenAprilTagTarget> targets, double timestampSeconds, long sequenceId, long latencyMicros,
            Optional<LumenMultiTagResult> multiTagResult, Optional<LumenConstrainedResult> constrainedResult) {
        super(targets, timestampSeconds, sequenceId, latencyMicros);
        this.multiTagResult = multiTagResult;
        this.constrainedResult = constrainedResult;
    }

    // the packet's tags; an object detection in it (a node of the other kind) is left out
    static LumenAprilTagResult from(LumenPipelineResult packet) {
        List<LumenAprilTagTarget> tags = new ArrayList<>(packet.getTargets().size());
        for (LumenTrackedTarget target : packet.getTargets()) {
            if (target.getFiducialId() >= 0) tags.add(new LumenAprilTagTarget(target));
        }
        return new LumenAprilTagResult(tags, packet.getTimestampSeconds(), packet.getSequenceId(), packet.latencyMicros(),
                packet.getMultiTagResult(), packet.getConstrainedResult());
    }

    /** The coprocessor's multi-tag PnP result for this frame: present when at least two tags with known field poses were seen. */
    public Optional<LumenMultiTagResult> getMultiTagResult() {
        return multiTagResult;
    }

    /**
     * The coprocessor's floor-constrained solve for this frame: present once the robot has published a seed pose and camera mount
     * ({@link LumenAprilTagDetector#setConstrainedSeed}) and a tag was seen.
     */
    public Optional<LumenConstrainedResult> getConstrainedResult() {
        return constrainedResult;
    }
}
