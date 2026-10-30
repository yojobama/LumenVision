package org.lumenvision.lib;

import edu.wpi.first.math.MatBuilder;
import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.Nat;
import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Quaternion;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.numbers.N1;
import edu.wpi.first.math.numbers.N3;
import edu.wpi.first.math.numbers.N8;
import edu.wpi.first.networktables.DoubleArrayPublisher;
import edu.wpi.first.networktables.DoubleArraySubscriber;
import edu.wpi.first.networktables.DoubleSubscriber;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.NetworkTablesJNI;
import edu.wpi.first.networktables.PubSubOption;
import edu.wpi.first.networktables.RawSubscriber;
import edu.wpi.first.networktables.StringSubscriber;
import edu.wpi.first.networktables.TimestampedRaw;
import edu.wpi.first.networktables.TimestampedString;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/**
 * What every detector node of the coprocessor's graph has: its results, connection, calibration and frame-rate cap. Open a node as the
 * kind it is, {@link LumenAprilTagDetector} or {@link LumenObjectDetector}; each gives its own kind of result.
 *
 * <p>Results come from the binary {@code <rootTable>/<node>/result} packet (see {@link LumenResultPacket}); controls are written to
 * {@code config/*} topics over NetworkTables. {@link LumenNode#setEnabled} and {@link LumenNode#takeSnapshot} apply as for any node.
 * Once the coprocessor's graph lists the node, a node opened as the other kind, or a name the graph does not have, is reported as a
 * driver-station warning.
 *
 * @param <R> the kind of result
 */
public abstract class LumenDetector<R extends LumenResult<?>> extends LumenNode {
    // the results a readQueue() can hold between calls; older ones are dropped
    private static final int UNREAD_QUEUE_DEPTH = 30;
    // heartbeat is refreshed each time the coprocessor publishes a result; no change for this long means it is gone
    private static final long HEARTBEAT_TIMEOUT_MICROS = 1_000_000;

    private static boolean versionCheckEnabled = true;

    // one subscription serves both: getAtomic() is the latest value and readQueue() the unread ones, independently
    private final RawSubscriber resultSub;

    private final IntegerPublisher fpsLimitPub;
    private final IntegerSubscriber fpsLimitSub;
    // created on the first seed, so only a detector that is sent one publishes the topics
    private DoubleArrayPublisher constrainedSeedPub;
    private DoubleArrayPublisher robotToCameraPub;

    private final DoubleArraySubscriber intrinsicsSub;
    private final DoubleArraySubscriber distortionSub;
    private final DoubleSubscriber heartbeatSub;
    private final StringSubscriber versionSub;
    private final IntegerSubscriber protocolSub;
    // checked at most once; the coprocessor's ".version" is constant for the life of its process
    private boolean versionChecked = false;
    private boolean protocolRefused = false;
    private boolean schemaWarned = false;

    // the coprocessor's kind for the node (LumenGraph.Node#getKind) this class reads, or null to read either
    private final String expectedKind;
    private final StringSubscriber graphSub;
    private long graphTimestamp = 0;
    private String nodeKind = null;
    private boolean missingWarned = false;

    /**
     * @param instance the NetworkTableInstance to use (injectable for simulation)
     * @param rootTable must match the coprocessor's NetworkTablesConfig.rootTable (default "lumenvision")
     * @param name the NT table the node's topics are under, which is its name in the LumenVision UI with characters outside
     *     {@code [A-Za-z0-9_.-]} replaced by {@code _} (see {@link LumenGraph.Node#getTable()})
     * @param expectedKind the kind of node this class reads, as the coprocessor's graph names it ({@link LumenGraph#APRILTAG_DETECTOR},
     *     {@link LumenGraph#OBJECT_DETECTOR}); null for a reader of either kind
     */
    protected LumenDetector(NetworkTableInstance instance, String rootTable, String name, String expectedKind) {
        super(instance, rootTable, name);
        this.expectedKind = expectedKind;
        String base = "/" + rootTable + "/" + tableName(name) + "/";
        String root = "/" + rootTable + "/";

        // send-all so the queue sees every frame, not just the latest per network send period
        resultSub = instance.getRawTopic(base + "result").subscribe("raw", new byte[0],
                PubSubOption.pollStorage(UNREAD_QUEUE_DEPTH), PubSubOption.keepDuplicates(true), PubSubOption.sendAll(true));

        fpsLimitPub = instance.getIntegerTopic(base + "config/fpsLimit").publish();
        fpsLimitSub = instance.getIntegerTopic(base + "status/fpsLimit").subscribe(-1);

        intrinsicsSub = instance.getDoubleArrayTopic(base + "cameraIntrinsics").subscribe(new double[0]);
        distortionSub = instance.getDoubleArrayTopic(base + "cameraDistortion").subscribe(new double[0]);
        heartbeatSub = instance.getDoubleTopic(root + "heartbeat").subscribe(0.0);
        versionSub = instance.getStringTopic(root + ".version").subscribe("");
        protocolSub = instance.getIntegerTopic(root + ".protocol").subscribe(-1);
        graphSub = instance.getStringTopic(root + ".graph").subscribe("");
    }

    /** Whether to compare this library's version and protocol with the coprocessor's and warn on a mismatch (default on). */
    public static void setVersionCheckEnabled(boolean enabled) {
        versionCheckEnabled = enabled;
    }

    /** This library's version, which the coprocessor's {@code .version} topic is compared with. */
    public static String getLibraryVersion() {
        return LumenVersionCheck.jarVersion();
    }

    /**
     * The most recent result. Never null; with no targets and sequence id 0 before the coprocessor has published anything, when
     * its result schema is unsupported, or when it speaks another protocol version.
     */
    public R getLatestResult() {
        verifyVersionOnce();
        verifyKind();
        if (protocolRefused) return toResult(emptyPacket());
        return toResult(decode(resultSub.getAtomic()).orElseGet(LumenDetector::emptyPacket));
    }

    /** Every result published since the last call (oldest first; at most {@value #UNREAD_QUEUE_DEPTH}), so none is missed between loops. */
    public List<R> getAllUnreadResults() {
        verifyVersionOnce();
        verifyKind();
        TimestampedRaw[] samples = resultSub.readQueue();
        List<R> results = new ArrayList<>(samples.length);
        if (protocolRefused) return results;
        for (TimestampedRaw sample : samples) decode(sample).map(this::toResult).ifPresent(results::add);
        return results;
    }

    /** This class's form of a decoded packet; the empty packet (no targets, sequence id 0) stands for no result. */
    protected abstract R toResult(LumenPipelineResult packet);

    /**
     * The node's kind as the coprocessor's graph names it ({@link LumenGraph.Node#getKind()}), once a result has been read after the graph
     * listed the node; empty before then, or for a node the graph does not list.
     */
    public Optional<String> getNodeKind() {
        return Optional.ofNullable(nodeKind);
    }

    private Optional<LumenPipelineResult> decode(TimestampedRaw sample) {
        if (sample.timestamp == 0 || sample.value.length == 0) return Optional.empty();
        // the coprocessor stamps the topic with the frame's capture time; NT converts it into this instance's clock
        final double timestampSeconds = sample.timestamp / 1_000_000.0;
        Optional<LumenResultPacket.Decoded> decoded = LumenResultPacket.decode(sample.value, timestampSeconds);
        if (decoded.isEmpty()) {
            if (!schemaWarned) {
                schemaWarned = true;
                LumenVersionCheck.report("LumenVision node '" + getName() + "' published a result this library cannot decode; update lumenlib to match the coprocessor.");
            }
            return Optional.empty();
        }
        LumenResultPacket.Decoded packet = decoded.get();
        return Optional.of(new LumenPipelineResult(packet.targets, timestampSeconds, packet.sequenceId,
                packet.latencyMicros, packet.multiTag, packet.constrained));
    }

    private static LumenPipelineResult emptyPacket() {
        return new LumenPipelineResult(new ArrayList<>(), 0.0, 0, 0, Optional.empty());
    }

    // reads the node's kind from the graph each time the graph changes, until the node is in it; warns once if it is not this class's kind
    private void verifyKind() {
        if (nodeKind != null) return;
        TimestampedString graphValue = graphSub.getAtomic();
        if (graphValue.timestamp == graphTimestamp) return;
        graphTimestamp = graphValue.timestamp;
        LumenGraph graph = LumenGraph.parse(graphValue.value);
        if (graph.getNodes().isEmpty()) return;
        Optional<LumenGraph.Node> node = graph.findByTable(tableName(getName())).filter(LumenGraph.Node::isDetector);
        if (node.isEmpty()) {
            if (expectedKind != null && !missingWarned) {
                missingWarned = true;
                LumenVersionCheck.report("LumenVision: the coprocessor has no detector named '" + getName() + "' (NT table '" + tableName(getName())
                        + "'); check the name against the LumenVision UI.");
            }
            return;
        }
        nodeKind = node.get().getKind();
        if (expectedKind != null && !expectedKind.equals(nodeKind)) {
            LumenVersionCheck.report("LumenVision: node '" + getName() + "' is " + describe(nodeKind) + " but was opened as " + describe(expectedKind)
                    + "; open it with " + (LumenGraph.OBJECT_DETECTOR.equals(nodeKind) ? "LumenObjectDetector" : "LumenAprilTagDetector")
                    + " to read its results.");
        }
    }

    private static String describe(String kind) {
        if (LumenGraph.APRILTAG_DETECTOR.equals(kind)) return "an AprilTag detector";
        if (LumenGraph.OBJECT_DETECTOR.equals(kind)) return "an object detector";
        return "a " + kind;
    }

    private void verifyVersionOnce() {
        if (versionChecked || !versionCheckEnabled) return;
        // deferred from the constructor: the NT4 handshake may not have completed yet, so ".version" would read empty
        String coprocessorVersion = versionSub.get();
        if (coprocessorVersion.isEmpty()) return;
        versionChecked = true;
        LumenVersionCheck.warnOnMismatch(coprocessorVersion);
        long protocol = protocolSub.get();
        if (protocol != LumenVersionCheck.PROTOCOL) {
            protocolRefused = true;
            LumenVersionCheck.report("LumenVision: the coprocessor speaks protocol " + (protocol < 0 ? "1-2 (none published)" : protocol)
                    + " but lumenlib-java needs " + LumenVersionCheck.PROTOCOL + "; results are ignored until both are updated together.");
        }
    }

    /** Compares this library's version and protocol with the coprocessor's now (once it has connected) and warns on a mismatch. */
    public void verifyVersion() {
        versionChecked = false;
        protocolRefused = false;
        boolean wasEnabled = versionCheckEnabled;
        versionCheckEnabled = true;
        try {
            verifyVersionOnce();
        } finally {
            versionCheckEnabled = wasEnabled;
        }
    }

    /** Whether the coprocessor is publishing: its heartbeat changed within the last second. */
    public boolean isConnected() {
        long stamp = heartbeatSub.getAtomic().timestamp; // 0 until a heartbeat has arrived
        return stamp != 0 && (NetworkTablesJNI.now() - stamp) < HEARTBEAT_TIMEOUT_MICROS;
    }

    // ---- control (written to config/*, read back from status/*) ----

    /** Publishes the floor-constrained solve's seed and camera mount (see {@link LumenAprilTagDetector#setConstrainedSeed}). */
    protected void publishConstrainedSeed(Pose2d robotPoseInLayoutFrame, Transform3d robotToCamera) {
        if (constrainedSeedPub == null) {
            String base = "/" + getRootTable() + "/" + tableName(getName()) + "/";
            constrainedSeedPub = getNetworkTableInstance().getDoubleArrayTopic(base + "config/constrainedSeed").publish();
            robotToCameraPub = getNetworkTableInstance().getDoubleArrayTopic(base + "config/robotToCamera").publish();
        }
        constrainedSeedPub.set(new double[] {
                robotPoseInLayoutFrame.getX(), robotPoseInLayoutFrame.getY(), robotPoseInLayoutFrame.getRotation().getRadians() });
        Quaternion q = robotToCamera.getRotation().getQuaternion();
        robotToCameraPub.set(new double[] {
                robotToCamera.getX(), robotToCamera.getY(), robotToCamera.getZ(), q.getW(), q.getX(), q.getY(), q.getZ() });
    }

    /** Caps how many results per second the node publishes; {@code <= 0} removes the cap. Disable it before a match. */
    public void setFpsLimit(int fps) {
        fpsLimitPub.set(fps);
    }

    /** The cap in effect, or -1 when unlimited (or before the coprocessor has reported). */
    public int getFpsLimit() {
        return (int) fpsLimitSub.get();
    }

    // ---- calibration ----

    /** The camera's 3x3 intrinsics matrix, once the coprocessor has calibration data for it. */
    public Optional<Matrix<N3, N3>> getCameraMatrix() {
        double[] k = intrinsicsSub.get();
        if (k.length != 9) return Optional.empty();
        return Optional.of(MatBuilder.fill(Nat.N3(), Nat.N3(), k));
    }

    /** The camera's distortion coefficients, zero-padded to 8, once the coprocessor has calibration data for it. */
    public Optional<Matrix<N8, N1>> getDistCoeffs() {
        double[] d = distortionSub.get();
        if (d.length == 0) return Optional.empty();
        double[] padded = new double[8];
        System.arraycopy(d, 0, padded, 0, Math.min(d.length, 8));
        return Optional.of(MatBuilder.fill(Nat.N8(), Nat.N1(), padded));
    }

    @Override
    public void close() {
        super.close();
        for (AutoCloseable closeable : new AutoCloseable[] {
                resultSub, fpsLimitPub, constrainedSeedPub, robotToCameraPub, fpsLimitSub, intrinsicsSub, distortionSub,
                heartbeatSub, versionSub, protocolSub, graphSub }) {
            if (closeable == null) continue; // the seed publishers of a detector never sent one
            try {
                closeable.close();
            } catch (Exception ignored) {
                // closing a topic handle cannot fail in a way the caller could act on
            }
        }
    }
}
