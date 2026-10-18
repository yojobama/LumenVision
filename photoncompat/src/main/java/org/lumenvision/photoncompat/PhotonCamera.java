package org.lumenvision.photoncompat;

import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.numbers.N1;
import edu.wpi.first.math.numbers.N3;
import edu.wpi.first.math.numbers.N8;
import edu.wpi.first.networktables.NetworkTableInstance;
import org.lumenvision.lib.LumenCoprocessor;
import org.lumenvision.lib.LumenDetector;
import org.lumenvision.lib.LumenGraph;
import org.lumenvision.lib.LumenNode;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;

/**
 * photonlib's PhotonCamera on top of LumenVision's nodes. {@code cameraName} is the camera's name in the LumenVision UI.
 *
 * <p>A pipeline is a detector node fed by the camera: {@link #setPipelineIndex} runs that node and stops the camera's other detectors.
 * Pipelines are the camera's detectors in id order, or the ones named in {@link #withPipelines}. Driver mode stops every detector of the
 * camera (its video keeps streaming), an input snapshot saves the camera's own frame and an output snapshot the running detector's.
 */
public class PhotonCamera implements AutoCloseable {
    private final NetworkTableInstance instance;
    private final String rootTable;
    private final String cameraName;
    private final LumenCoprocessor coprocessor;
    private final Map<String, LumenDetector> detectors = new HashMap<>();
    private final Map<String, LumenNode> nodes = new HashMap<>();

    private String[] pipelineNames;
    private int selected = -1;
    private boolean driverMode;

    /** The default NT4 instance and root table "lumenvision". */
    public PhotonCamera(String cameraName) {
        this(NetworkTableInstance.getDefault(), LumenCoprocessor.DEFAULT_ROOT_TABLE, cameraName);
    }

    public PhotonCamera(NetworkTableInstance instance, String rootTable, String cameraName) {
        this.instance = instance;
        this.rootTable = rootTable;
        this.cameraName = cameraName;
        this.coprocessor = new LumenCoprocessor(instance, rootTable);
    }

    /** Names the detector nodes that are this camera's pipelines, in pipeline-index order (as shown in the LumenVision UI). */
    public PhotonCamera withPipelines(String... detectorNames) {
        this.pipelineNames = detectorNames.clone();
        return this;
    }

    // the pipelines' tables: the named or camera-fed detectors, else the one detector published under the camera's own name
    private List<LumenGraph.Node> pipelines() {
        LumenGraph graph = coprocessor.getGraph();
        List<LumenGraph.Node> found = new ArrayList<>();
        if (pipelineNames != null) {
            for (String name : pipelineNames) {
                graph.getNodes().stream().filter(n -> n.isDetector() && (n.getName().equals(name) || n.getTable().equals(name)))
                        .findFirst().ifPresent(found::add);
            }
        } else {
            graph.findCamera(cameraName).ifPresent(camera -> found.addAll(graph.detectorsOf(camera.getId())));
        }
        return found;
    }

    private LumenDetector detector(String table) {
        return detectors.computeIfAbsent(table, t -> new LumenDetector(instance, rootTable, t));
    }

    private LumenNode node(String table) {
        return nodes.computeIfAbsent(table, t -> new LumenNode(instance, rootTable, t));
    }

    // the table results are read from: the selected pipeline's, else the first enabled one's, else the camera's own name
    private String currentTable() {
        List<LumenGraph.Node> pipelines = pipelines();
        if (pipelines.isEmpty()) return LumenNode.tableName(cameraName);
        return pipelines.get(getPipelineIndex(pipelines)).getTable();
    }

    private int getPipelineIndex(List<LumenGraph.Node> pipelines) {
        if (selected >= 0 && selected < pipelines.size()) return selected;
        for (int i = 0; i < pipelines.size(); i++) if (pipelines.get(i).isEnabled()) return i;
        return 0;
    }

    /** The most recent result of the current pipeline. */
    public PhotonPipelineResult getLatestResult() {
        return new PhotonPipelineResult(detector(currentTable()).getLatestResult());
    }

    /** Every result the current pipeline published since the last call, oldest first. */
    public List<PhotonPipelineResult> getAllUnreadResults() {
        return detector(currentTable()).getAllUnreadResults().stream().map(PhotonPipelineResult::new).collect(Collectors.toList());
    }

    /** Driver mode stops the camera's detectors, so no results are published; the camera itself keeps running. */
    public void setDriverMode(boolean driverMode) {
        this.driverMode = driverMode;
        applyRunningState();
    }

    public boolean getDriverMode() {
        List<LumenGraph.Node> pipelines = pipelines();
        if (pipelines.isEmpty()) return !detector(LumenNode.tableName(cameraName)).isEnabled();
        return pipelines.stream().noneMatch(LumenGraph.Node::isEnabled);
    }

    /** Runs pipeline {@code index} and stops the camera's other pipelines. */
    public void setPipelineIndex(int index) {
        this.selected = index;
        applyRunningState();
    }

    public int getPipelineIndex() {
        List<LumenGraph.Node> pipelines = pipelines();
        return pipelines.isEmpty() ? 0 : getPipelineIndex(pipelines);
    }

    private void applyRunningState() {
        List<LumenGraph.Node> pipelines = pipelines();
        if (pipelines.isEmpty()) {
            detector(LumenNode.tableName(cameraName)).setEnabled(!driverMode);
            return;
        }
        int running = getPipelineIndex(pipelines);
        for (int i = 0; i < pipelines.size(); i++) detector(pipelines.get(i).getTable()).setEnabled(!driverMode && i == running);
    }

    /** Saves the camera's own frame on the coprocessor. */
    public void takeInputSnapshot() {
        coprocessor.getGraph().findCamera(cameraName).ifPresent(camera -> node(camera.getTable()).takeSnapshot());
    }

    /** Saves the current pipeline's annotated frame on the coprocessor. */
    public void takeOutputSnapshot() {
        detector(currentTable()).takeSnapshot();
    }

    public void setLED(VisionLEDMode led) {
        coprocessor.setLed(led.toLumen());
    }

    public VisionLEDMode getLEDMode() {
        return VisionLEDMode.fromLumen(coprocessor.getLed());
    }

    public void setFPSLimit(int fps) {
        detector(currentTable()).setFpsLimit(fps);
    }

    public int getFPSLimit() {
        return detector(currentTable()).getFpsLimit();
    }

    public boolean isConnected() {
        return detector(currentTable()).isConnected();
    }

    public Optional<Matrix<N3, N3>> getCameraMatrix() {
        return detector(currentTable()).getCameraMatrix();
    }

    public Optional<Matrix<N8, N1>> getDistCoeffs() {
        return detector(currentTable()).getDistCoeffs();
    }

    public String getName() {
        return cameraName;
    }

    public static void setVersionCheckEnabled(boolean enabled) {
        LumenDetector.setVersionCheckEnabled(enabled);
    }

    public void verifyVersion() {
        detector(currentTable()).verifyVersion();
    }

    /** The current pipeline's detector node, for features with no PhotonVision equivalent. */
    public LumenDetector getLumenDetector() {
        return detector(currentTable());
    }

    @Override
    public void close() {
        detectors.values().forEach(LumenDetector::close);
        nodes.values().forEach(LumenNode::close);
    }
}
