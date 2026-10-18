package org.lumenvision.photoncompat;

import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.numbers.N1;
import edu.wpi.first.math.numbers.N3;
import edu.wpi.first.math.numbers.N8;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
import org.lumenvision.lib.LumenCoprocessor;
import org.lumenvision.lib.LumenDetector;
import org.lumenvision.lib.LumenGraph;
import org.lumenvision.lib.LumenNode;

import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;

/**
 * photonlib's PhotonCamera on top of LumenVision's nodes. {@code cameraName} is the camera's name in the LumenVision UI.
 *
 * <p>Driver mode stops the camera's detector (its video keeps streaming from the camera), an input snapshot saves the camera's own
 * frame and an output snapshot the detector's annotated one.
 */
public class PhotonCamera implements AutoCloseable {
    private final NetworkTableInstance instance;
    private final String rootTable;
    private final String cameraName;
    private final LumenDetector detector;
    private final LumenCoprocessor coprocessor;

    private final IntegerPublisher pipelineIndexPub;
    private final IntegerSubscriber pipelineIndexSub;
    private LumenNode cameraNode;

    /** The default NT4 instance and root table "lumenvision". */
    public PhotonCamera(String cameraName) {
        this(NetworkTableInstance.getDefault(), LumenCoprocessor.DEFAULT_ROOT_TABLE, cameraName);
    }

    public PhotonCamera(NetworkTableInstance instance, String rootTable, String cameraName) {
        this.instance = instance;
        this.rootTable = rootTable;
        this.cameraName = cameraName;
        this.detector = new LumenDetector(instance, rootTable, cameraName);
        this.coprocessor = new LumenCoprocessor(instance, rootTable);
        String base = "/" + rootTable + "/" + LumenNode.tableName(cameraName) + "/";
        pipelineIndexPub = instance.getIntegerTopic(base + "config/pipelineIndex").publish();
        pipelineIndexSub = instance.getIntegerTopic(base + "status/pipelineIndex").subscribe(-1);
    }

    /** The most recent result. */
    public PhotonPipelineResult getLatestResult() {
        return new PhotonPipelineResult(detector.getLatestResult());
    }

    /** Every result published since the last call, oldest first. */
    public List<PhotonPipelineResult> getAllUnreadResults() {
        return detector.getAllUnreadResults().stream().map(PhotonPipelineResult::new).collect(Collectors.toList());
    }

    /** Driver mode stops the camera's detector, so no results are published; the camera itself keeps running. */
    public void setDriverMode(boolean driverMode) {
        detector.setEnabled(!driverMode);
    }

    public boolean getDriverMode() {
        return !detector.isEnabled();
    }

    public void setPipelineIndex(int index) {
        pipelineIndexPub.set(index);
    }

    public int getPipelineIndex() {
        return (int) pipelineIndexSub.get();
    }

    /** Saves the camera's own frame on the coprocessor. */
    public void takeInputSnapshot() {
        LumenNode camera = cameraNode();
        if (camera != null) camera.takeSnapshot();
    }

    /** Saves the detector's annotated frame on the coprocessor. */
    public void takeOutputSnapshot() {
        detector.takeSnapshot();
    }

    // the camera's own node, found in the coprocessor's graph by name and addressed by its id
    private LumenNode cameraNode() {
        if (cameraNode == null) {
            Optional<LumenGraph.Node> camera = coprocessor.getGraph().findCamera(cameraName);
            camera.ifPresent(node -> cameraNode = new LumenNode(instance, rootTable, String.valueOf(node.getId())));
        }
        return cameraNode;
    }

    public void setLED(VisionLEDMode led) {
        coprocessor.setLed(led.toLumen());
    }

    public VisionLEDMode getLEDMode() {
        return VisionLEDMode.fromLumen(coprocessor.getLed());
    }

    public void setFPSLimit(int fps) {
        detector.setFpsLimit(fps);
    }

    public int getFPSLimit() {
        return detector.getFpsLimit();
    }

    public boolean isConnected() {
        return detector.isConnected();
    }

    public Optional<Matrix<N3, N3>> getCameraMatrix() {
        return detector.getCameraMatrix();
    }

    public Optional<Matrix<N8, N1>> getDistCoeffs() {
        return detector.getDistCoeffs();
    }

    public String getName() {
        return cameraName;
    }

    public static void setVersionCheckEnabled(boolean enabled) {
        LumenDetector.setVersionCheckEnabled(enabled);
    }

    public void verifyVersion() {
        detector.verifyVersion();
    }

    /** The camera's detector node, for features with no PhotonVision equivalent. */
    public LumenDetector getLumenDetector() {
        return detector;
    }

    @Override
    public void close() {
        detector.close();
        pipelineIndexPub.close();
        pipelineIndexSub.close();
        if (cameraNode != null) cameraNode.close();
    }
}
