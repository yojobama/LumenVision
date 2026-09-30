package org.lumenvision.photoncompat.compat;

import org.lumenvision.photoncompat.LumenPipelineResult;

/** Timing information about a result, as photonlib's PhotonPipelineMetadata. */
public class PhotonPipelineMetadata {
    private final LumenPipelineResult result;

    PhotonPipelineMetadata(LumenPipelineResult result) {
        this.result = result;
    }

    /** The coprocessor's per-camera frame counter. */
    public long getSequenceID() {
        return result.getSequenceId();
    }

    /** When the frame was captured, in microseconds on the robot's clock. */
    public long getCaptureTimestampMicros() {
        return result.getCaptureTimestampMicros();
    }

    /** When the coprocessor published the result, in microseconds on the robot's clock. */
    public long getPublishTimestampMicros() {
        return result.getPublishTimestampMicros();
    }

    /** Not measured by LumenVision (there is no ping/pong exchange); always 0. */
    public long getTimeSinceLastPong() {
        return 0;
    }
}
