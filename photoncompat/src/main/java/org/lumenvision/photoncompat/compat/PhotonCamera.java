package org.lumenvision.photoncompat.compat;

import edu.wpi.first.networktables.NetworkTableInstance;
import org.lumenvision.photoncompat.LumenCamera;

/**
 * Thin wrapper giving {@link LumenCamera} PhotonVision's class name and shape for drop-in
 * migration; every method delegates to the underlying {@link LumenCamera}.
 */
public class PhotonCamera {
    private final LumenCamera lumenCamera;

    /**
     * Matches {@code PhotonCamera(String cameraName)}: the default NT4 instance and root table
     * "lumenvision".
     */
    public PhotonCamera(String cameraName) {
        this(NetworkTableInstance.getDefault(), "lumenvision", cameraName);
    }

    public PhotonCamera(NetworkTableInstance instance, String rootTable, String cameraName) {
        this.lumenCamera = new LumenCamera(instance, rootTable, cameraName);
    }

    public PhotonPipelineResult getLatestResult() {
        return new PhotonPipelineResult(lumenCamera.getLatestResult());
    }

    /** The underlying {@link LumenCamera}, for features with no PhotonVision equivalent (e.g. multi-tag). */
    public LumenCamera getLumenCamera() {
        return lumenCamera;
    }
}
