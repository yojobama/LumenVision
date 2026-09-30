package org.lumenvision.photoncompat;

import edu.wpi.first.math.MatBuilder;
import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.Nat;
import edu.wpi.first.math.numbers.N1;
import edu.wpi.first.math.numbers.N3;
import edu.wpi.first.math.numbers.N8;
import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Quaternion;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.DoubleArrayPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
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
 * Robot-side client for one LumenVision coprocessor camera, mirroring photonlib's PhotonCamera.
 *
 * <p>Results come from the binary {@code <rootTable>/<camera>/result} packet (see {@link LumenResultPacket}); control (pipeline,
 * driver mode, FPS limit, snapshots, LED) is written to {@code config/*} topics and read back from {@code status/*}, all over
 * NetworkTables. The {@code <camera>} table is the camera's name in the coprocessor UI, with characters outside
 * {@code [A-Za-z0-9_.-]} replaced by {@code _} (see {@link #tableName(String)}).
 */
public class LumenCamera implements AutoCloseable {
    // the results a readQueue() can hold between calls; older ones are dropped
    private static final int UNREAD_QUEUE_DEPTH = 30;
    // heartbeat is refreshed each time the coprocessor publishes a result; no change for this long means it is gone
    private static final long HEARTBEAT_TIMEOUT_MICROS = 1_000_000;

    private static boolean versionCheckEnabled = true;

    private final NetworkTableInstance instance;
    private final String rootTable;
    private final String name;

    // one subscription serves both: getAtomic() is the latest value and readQueue() the unread ones, independently
    private final RawSubscriber resultSub;

    private final IntegerPublisher pipelineIndexPub;
    private final BooleanPublisher driverModePub;
    private final IntegerPublisher fpsLimitPub;
    private final IntegerPublisher inputSnapshotPub;
    private final IntegerPublisher outputSnapshotPub;
    private final IntegerPublisher ledModePub;
    private final DoubleArrayPublisher constrainedSeedPub;
    private final DoubleArrayPublisher robotToCameraPub;
    private long inputSnapshotCount;
    private long outputSnapshotCount;

    private final IntegerSubscriber pipelineIndexSub;
    private final BooleanSubscriber driverModeSub;
    private final IntegerSubscriber fpsLimitSub;
    private final IntegerSubscriber ledModeSub;

    private final DoubleArraySubscriber intrinsicsSub;
    private final DoubleArraySubscriber distortionSub;
    private final DoubleSubscriber heartbeatSub;
    private final StringSubscriber versionSub;
    // checked at most once; the coprocessor's ".version" is constant for the life of its process
    private boolean versionChecked = false;
    private boolean schemaWarned = false;

    /**
     * @param instance the NetworkTableInstance to use (injectable for simulation)
     * @param rootTable must match the coprocessor's NetworkTablesConfig.rootTable (default "lumenvision")
     * @param cameraName the camera's name as shown in the coprocessor UI
     */
    public LumenCamera(NetworkTableInstance instance, String rootTable, String cameraName) {
        this.instance = instance;
        this.rootTable = rootTable;
        this.name = cameraName;
        String base = "/" + rootTable + "/" + tableName(cameraName) + "/";
        String root = "/" + rootTable + "/";

        // send-all so the queue sees every frame, not just the latest per network send period
        resultSub = instance.getRawTopic(base + "result").subscribe("raw", new byte[0],
                PubSubOption.pollStorage(UNREAD_QUEUE_DEPTH), PubSubOption.keepDuplicates(true), PubSubOption.sendAll(true));

        pipelineIndexPub = instance.getIntegerTopic(base + "config/pipelineIndex").publish();
        driverModePub = instance.getBooleanTopic(base + "config/driverMode").publish();
        fpsLimitPub = instance.getIntegerTopic(base + "config/fpsLimit").publish();
        inputSnapshotPub = instance.getIntegerTopic(base + "config/inputSnapshot").publish();
        outputSnapshotPub = instance.getIntegerTopic(base + "config/outputSnapshot").publish();
        ledModePub = instance.getIntegerTopic(root + "config/ledMode").publish();
        constrainedSeedPub = instance.getDoubleArrayTopic(base + "config/constrainedSeed").publish();
        robotToCameraPub = instance.getDoubleArrayTopic(base + "config/robotToCamera").publish();

        pipelineIndexSub = instance.getIntegerTopic(base + "status/pipelineIndex").subscribe(-1);
        driverModeSub = instance.getBooleanTopic(base + "status/driverMode").subscribe(false);
        fpsLimitSub = instance.getIntegerTopic(base + "status/fpsLimit").subscribe(-1);
        ledModeSub = instance.getIntegerTopic(root + "status/ledMode").subscribe(-1);

        intrinsicsSub = instance.getDoubleArrayTopic(base + "cameraIntrinsics").subscribe(new double[0]);
        distortionSub = instance.getDoubleArrayTopic(base + "cameraDistortion").subscribe(new double[0]);
        heartbeatSub = instance.getDoubleTopic(root + "heartbeat").subscribe(0.0);
        versionSub = instance.getStringTopic(root + ".version").subscribe("");
    }

    /** The NT table name a camera's topics are published under: its name with characters outside {@code [A-Za-z0-9_.-]} replaced by {@code _}. */
    public static String tableName(String cameraName) {
        StringBuilder out = new StringBuilder(cameraName.length());
        for (int i = 0; i < cameraName.length(); i++) {
            char c = cameraName.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            out.append(ok ? c : '_');
        }
        return out.toString();
    }

    /** Whether to compare this library's version with the coprocessor's and warn on a mismatch (default on). */
    public static void setVersionCheckEnabled(boolean enabled) {
        versionCheckEnabled = enabled;
    }

    public String getName() {
        return name;
    }

    /** The NetworkTables instance this camera reads and writes (the simulation publishes into the same one). */
    public NetworkTableInstance getNetworkTableInstance() {
        return instance;
    }

    /** The NT root table, e.g. "lumenvision". */
    public String getRootTable() {
        return rootTable;
    }

    /** This library's version, which the coprocessor's {@code .version} topic is compared with. */
    public static String getLibraryVersion() {
        return LumenVersionCheck.jarVersion();
    }

    /**
     * The most recent result. Never null; with no targets and sequence id 0 before the coprocessor has published anything (or when
     * its result schema is unsupported).
     */
    public LumenPipelineResult getLatestResult() {
        verifyVersionOnce();
        return decode(resultSub.getAtomic()).orElseGet(LumenCamera::emptyResult);
    }

    /** Every result published since the last call (oldest first; at most {@value #UNREAD_QUEUE_DEPTH}), so none is missed between loops. */
    public List<LumenPipelineResult> getAllUnreadResults() {
        verifyVersionOnce();
        TimestampedRaw[] samples = resultSub.readQueue();
        List<LumenPipelineResult> results = new ArrayList<>(samples.length);
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
                LumenVersionCheck.report("LumenVision camera '" + name + "' published a result this library cannot decode; update photoncompat to match the coprocessor.");
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
        if (!coprocessorVersion.isEmpty()) {
            LumenVersionCheck.warnOnMismatch(coprocessorVersion);
            versionChecked = true;
        }
    }

    /** Compares this library's version with the coprocessor's now (once it has connected) and warns on a mismatch. */
    public void verifyVersion() {
        versionChecked = false;
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
     * {@link LumenPipelineResult#getConstrainedResult()}. {@link LumenPoseEstimator} does this for CONSTRAINED_SOLVEPNP.
     */
    public void setConstrainedSeed(Pose2d robotPoseInLayoutFrame, Transform3d robotToCamera) {
        constrainedSeedPub.set(new double[] {
                robotPoseInLayoutFrame.getX(), robotPoseInLayoutFrame.getY(), robotPoseInLayoutFrame.getRotation().getRadians() });
        Quaternion q = robotToCamera.getRotation().getQuaternion();
        robotToCameraPub.set(new double[] {
                robotToCamera.getX(), robotToCamera.getY(), robotToCamera.getZ(), q.getW(), q.getX(), q.getY(), q.getZ() });
    }

    /** Activates the pipeline profile with this index on the camera. */
    public void setPipelineIndex(int index) {
        pipelineIndexPub.set(index);
    }

    /** The pipeline index the coprocessor reports in effect, or -1 before it has reported. */
    public int getPipelineIndex() {
        return (int) pipelineIndexSub.get();
    }

    /** Driver mode streams the raw camera image with no detection or result publishing. */
    public void setDriverMode(boolean enabled) {
        driverModePub.set(enabled);
    }

    public boolean getDriverMode() {
        return driverModeSub.get();
    }

    /** Caps how many results per second the camera publishes; {@code <= 0} removes the cap. Disable it before a match. */
    public void setFPSLimit(int fps) {
        fpsLimitPub.set(fps);
    }

    /** The FPS cap in effect, or -1 when unlimited (or before the coprocessor has reported). */
    public int getFPSLimit() {
        return (int) fpsLimitSub.get();
    }

    /** Saves the raw camera frame on the coprocessor. */
    public void takeInputSnapshot() {
        inputSnapshotPub.set(++inputSnapshotCount);
    }

    /** Saves the annotated frame of the active pipeline on the coprocessor. */
    public void takeOutputSnapshot() {
        outputSnapshotPub.set(++outputSnapshotCount);
    }

    /** Sets the coprocessor-wide vision LED mode (not per camera). */
    public void setLED(LumenLedMode mode) {
        ledModePub.set(mode.value);
    }

    /** The LED mode the coprocessor reports applied. */
    public LumenLedMode getLED() {
        return LumenLedMode.fromValue((int) ledModeSub.get());
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
        for (AutoCloseable closeable : new AutoCloseable[] {
                resultSub, pipelineIndexPub, driverModePub, fpsLimitPub, inputSnapshotPub, outputSnapshotPub,
                ledModePub, constrainedSeedPub, robotToCameraPub, pipelineIndexSub, driverModeSub, fpsLimitSub, ledModeSub, intrinsicsSub, distortionSub, heartbeatSub,
                versionSub }) {
            try {
                closeable.close();
            } catch (Exception ignored) {
                // closing a topic handle cannot fail in a way the caller could act on
            }
        }
    }
}
