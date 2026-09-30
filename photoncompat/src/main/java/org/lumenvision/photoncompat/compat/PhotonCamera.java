package org.lumenvision.photoncompat.compat;

import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.numbers.N1;
import edu.wpi.first.math.numbers.N3;
import edu.wpi.first.math.numbers.N8;
import edu.wpi.first.networktables.NetworkTableInstance;
import org.lumenvision.photoncompat.LumenCamera;

import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;

/**
 * {@link LumenCamera} with photonlib's PhotonCamera class name and method names, for drop-in migration. {@code cameraName} is the
 * camera's name in the LumenVision UI.
 */
public class PhotonCamera implements AutoCloseable {
    private final LumenCamera lumenCamera;

    /** The default NT4 instance and root table "lumenvision". */
    public PhotonCamera(String cameraName) {
        this(NetworkTableInstance.getDefault(), "lumenvision", cameraName);
    }

    public PhotonCamera(NetworkTableInstance instance, String rootTable, String cameraName) {
        this.lumenCamera = new LumenCamera(instance, rootTable, cameraName);
    }

    /** The most recent result. */
    public PhotonPipelineResult getLatestResult() {
        return new PhotonPipelineResult(lumenCamera.getLatestResult());
    }

    /** Every result published since the last call, oldest first. */
    public List<PhotonPipelineResult> getAllUnreadResults() {
        return lumenCamera.getAllUnreadResults().stream().map(PhotonPipelineResult::new).collect(Collectors.toList());
    }

    public void setDriverMode(boolean driverMode) {
        lumenCamera.setDriverMode(driverMode);
    }

    public boolean getDriverMode() {
        return lumenCamera.getDriverMode();
    }

    public void setPipelineIndex(int index) {
        lumenCamera.setPipelineIndex(index);
    }

    public int getPipelineIndex() {
        return lumenCamera.getPipelineIndex();
    }

    public void takeInputSnapshot() {
        lumenCamera.takeInputSnapshot();
    }

    public void takeOutputSnapshot() {
        lumenCamera.takeOutputSnapshot();
    }

    public void setLED(VisionLEDMode led) {
        lumenCamera.setLED(led.toLumen());
    }

    public VisionLEDMode getLEDMode() {
        return VisionLEDMode.fromLumen(lumenCamera.getLED());
    }

    public void setFPSLimit(int fps) {
        lumenCamera.setFPSLimit(fps);
    }

    public int getFPSLimit() {
        return lumenCamera.getFPSLimit();
    }

    public boolean isConnected() {
        return lumenCamera.isConnected();
    }

    public Optional<Matrix<N3, N3>> getCameraMatrix() {
        return lumenCamera.getCameraMatrix();
    }

    public Optional<Matrix<N8, N1>> getDistCoeffs() {
        return lumenCamera.getDistCoeffs();
    }

    public String getName() {
        return lumenCamera.getName();
    }

    public static void setVersionCheckEnabled(boolean enabled) {
        LumenCamera.setVersionCheckEnabled(enabled);
    }

    public void verifyVersion() {
        lumenCamera.verifyVersion();
    }

    /** The underlying {@link LumenCamera}, for features with no PhotonVision equivalent. */
    public LumenCamera getLumenCamera() {
        return lumenCamera;
    }

    @Override
    public void close() {
        lumenCamera.close();
    }
}
