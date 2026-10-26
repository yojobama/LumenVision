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

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/**
 * A detector node (AprilTag or object detection) of the coprocessor's graph: its results, calibration and tuning.
 *
 * <p>Results come from the binary {@code <rootTable>/<node>/result} packet (see {@link LumenResultPacket}); controls are written to
 * {@code config/*} topics over NetworkTables. {@link LumenNode#setEnabled} and {@link LumenNode#takeSnapshot} apply as for any node.
 */
public class LumenDetector extends LumenNode {
    // the results a readQueue() can hold between calls; older ones are dropped
    private static final int UNREAD_QUEUE_DEPTH = 30;
    // heartbeat is refreshed each time the coprocessor publishes a result; no change for this long means it is gone
    private static final long HEARTBEAT_TIMEOUT_MICROS = 1_000_000;

    private static boolean versionCheckEnabled = true;

    // one subscription serves both: getAtomic() is the latest value and readQueue() the unread ones, independently
    private final RawSubscriber resultSub;

    private final IntegerPublisher fpsLimitPub;
    private final DoubleArrayPublisher constrainedSeedPub;
    private final DoubleArrayPublisher robotToCameraPub;
    private final IntegerSubscriber fpsLimitSub;

    private final DoubleArraySubscriber intrinsicsSub;
    private final DoubleArraySubscriber distortionSub;
    private final DoubleSubscriber heartbeatSub;
    private final StringSubscriber versionSub;
    private final IntegerSubscriber protocolSub;
    // checked at most once; the coprocessor's ".version" is constant for the life of its process
    private boolean versionChecked = false;
    private boolean protocolRefused = false;
    private boolean schemaWarned = false;

    /** The default NT4 instance and root table "lumenvision". */
    public LumenDetector(String name) {
        this(NetworkTableInstance.getDefault(), LumenCoprocessor.DEFAULT_ROOT_TABLE, name);
    }

    public LumenDetector(NetworkTableInstance instance, String rootTable, String name) {
        super(instance, rootTable, name);
        String base = "/" + rootTable + "/" + tableName(name) + "/";
        String root = "/" + rootTable + "/";

        // send-all so the queue sees every frame, not just the latest per network send period
        resultSub = instance.getRawTopic(base + "result").subscribe("raw", new byte[0],
                PubSubOption.pollStorage(UNREAD_QUEUE_DEPTH), PubSubOption.keepDuplicates(true), PubSubOption.sendAll(true));

        fpsLimitPub = instance.getIntegerTopic(base + "config/fpsLimit").publish();
        constrainedSeedPub = instance.getDoubleArrayTopic(base + "config/constrainedSeed").publish();
        robotToCameraPub = instance.getDoubleArrayTopic(base + "config/robotToCamera").publish();
        fpsLimitSub = instance.getIntegerTopic(base + "status/fpsLimit").subscribe(-1);

        intrinsicsSub = instance.getDoubleArrayTopic(base + "cameraIntrinsics").subscribe(new double[0]);
        distortionSub = instance.getDoubleArrayTopic(base + "cameraDistortion").subscribe(new double[0]);
        heartbeatSub = instance.getDoubleTopic(root + "heartbeat").subscribe(0.0);
        versionSub = instance.getStringTopic(root + ".version").subscribe("");
        protocolSub = instance.getIntegerTopic(root + ".protocol").subscribe(-1);
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
    public LumenPipelineResult getLatestResult() {
        verifyVersionOnce();
        if (protocolRefused) return emptyResult();
        return decode(resultSub.getAtomic()).orElseGet(LumenDetector::emptyResult);
    }

    /** Every result published since the last call (oldest first; at most {@value #UNREAD_QUEUE_DEPTH}), so none is missed between loops. */
    public List<LumenPipelineResult> getAllUnreadResults() {
        verifyVersionOnce();
        TimestampedRaw[] samples = resultSub.readQueue();
        List<LumenPipelineResult> results = new ArrayList<>(samples.length);
        if (protocolRefused) return results;
        for (TimestampedRaw sample : samples) decode(sample).ifPresent(results::add);
        return results;
    }

    /** The multi-tag result of the latest frame, if the coprocessor solved one. */
    public Optional<LumenMultiTagResult> getMultiTagResult() {
        return getLatestResult().getMultiTagResult();
    }

    private Optional<LumenPipelineResult> decode(TimestampedRaw sample) {
        if (sample.timestamp == 0 || sample.value.length == 0) return Optional.empty();
        Optional<LumenResultPacket.Decoded> decoded = LumenResultPacket.decode(sample.value);
        if (decoded.isEmpty()) {
            if (!schemaWarned) {
                schemaWarned = true;
                LumenVersionCheck.report("LumenVision node '" + getName() + "' published a result this library cannot decode; update lumenlib to match the coprocessor.");
            }
            return Optional.empty();
        }
        LumenResultPacket.Decoded packet = decoded.get();
        // the coprocessor stamps the topic with the frame's capture time; NT converts it into this instance's clock
        return Optional.of(new LumenPipelineResult(packet.targets, sample.timestamp / 1_000_000.0, packet.sequenceId,
                packet.latencyMicros, packet.multiTag, packet.constrained));
    }

    private static LumenPipelineResult emptyResult() {
        return new LumenPipelineResult(new ArrayList<>(), 0.0, 0, 0, Optional.empty());
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

    /**
     * Starts (and keeps current) the coprocessor's floor-constrained solve: the starting robot pose, in the coprocessor's field-layout
     * frame, and where the camera is mounted on the robot. Call every loop with the latest estimate; the result appears in
     * {@link LumenPipelineResult#getConstrainedResult()}.
     */
    public void setConstrainedSeed(Pose2d robotPoseInLayoutFrame, Transform3d robotToCamera) {
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
                heartbeatSub, versionSub, protocolSub }) {
            try {
                closeable.close();
            } catch (Exception ignored) {
                // closing a topic handle cannot fail in a way the caller could act on
            }
        }
    }
}
