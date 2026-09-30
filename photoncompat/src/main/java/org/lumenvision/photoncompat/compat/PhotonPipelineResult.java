package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.geometry.Transform3d;
import org.lumenvision.photoncompat.LumenMultiTagResult;
import org.lumenvision.photoncompat.LumenPipelineResult;

import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;

/** {@link LumenPipelineResult} with photonlib's PhotonPipelineResult method names, for drop-in migration. */
public class PhotonPipelineResult {
    private final LumenPipelineResult lumenResult;

    PhotonPipelineResult(LumenPipelineResult lumenResult) {
        this.lumenResult = lumenResult;
    }

    /** Targets, largest image area first. */
    public List<PhotonTrackedTarget> getTargets() {
        return lumenResult.getTargets().stream().map(PhotonTrackedTarget::new).collect(Collectors.toList());
    }

    public boolean hasTargets() {
        return lumenResult.hasTargets();
    }

    /** The largest target, or null when there are none. */
    public PhotonTrackedTarget getBestTarget() {
        return lumenResult.getBestTarget().map(PhotonTrackedTarget::new).orElse(null);
    }

    /** When the frame was captured, in seconds on the robot's clock. */
    public double getTimestampSeconds() {
        return lumenResult.getTimestampSeconds();
    }

    public double getLatencyMillis() {
        return lumenResult.getLatencyMillis();
    }

    public PhotonPipelineMetadata metadata() {
        return new PhotonPipelineMetadata(lumenResult);
    }

    public PhotonPipelineMetadata getMetadata() {
        return metadata();
    }

    public Optional<MultiTargetPNPResult> getMultiTagResult() {
        Optional<LumenMultiTagResult> multiTag = lumenResult.getMultiTagResult();
        return multiTag.map(m -> {
            Transform3d fieldToCamera = new Transform3d(m.getFieldToCamera().getTranslation(), m.getFieldToCamera().getRotation());
            PnpResult pnp = new PnpResult(fieldToCamera, fieldToCamera, m.getReprojectionErrorPixels(), m.getReprojectionErrorPixels(), 0.0);
            List<Short> ids = m.getFiducialIds().stream().map(Integer::shortValue).collect(Collectors.toList());
            return new MultiTargetPNPResult(pnp, ids);
        });
    }

    public LumenPipelineResult getLumenPipelineResult() {
        return lumenResult;
    }
}
