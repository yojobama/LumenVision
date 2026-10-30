package org.lumenvision.lib;

import java.util.ArrayList;
import java.util.List;

/** One frame's results from a {@link LumenObjectDetector}: the objects the model found, largest first. */
public class LumenObjectDetectionResult extends LumenResult<LumenDetectedObject> {
    /**
     * @param objects the detections, largest image area first
     * @param timestampSeconds the frame's capture time (see {@link #getTimestampSeconds()}); 0 for none, e.g. in a test
     */
    public LumenObjectDetectionResult(List<LumenDetectedObject> objects, double timestampSeconds, long sequenceId, long latencyMicros) {
        super(objects, timestampSeconds, sequenceId, latencyMicros);
    }

    // the packet's objects; a tag in it (a node of the other kind) is left out
    static LumenObjectDetectionResult from(LumenPipelineResult packet) {
        List<LumenDetectedObject> objects = new ArrayList<>(packet.getTargets().size());
        for (LumenTrackedTarget target : packet.getTargets()) {
            if (target.getDetectedObjectClassID() >= 0) objects.add(new LumenDetectedObject(target));
        }
        return new LumenObjectDetectionResult(objects, packet.getTimestampSeconds(), packet.getSequenceId(), packet.latencyMicros());
    }

    /** The detections of one class, largest first. */
    public List<LumenDetectedObject> getTargetsOfClass(int classId) {
        List<LumenDetectedObject> out = new ArrayList<>();
        for (LumenDetectedObject object : getTargets()) if (object.getClassId() == classId) out.add(object);
        return out;
    }
}
