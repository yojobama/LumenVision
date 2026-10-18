package org.lumenvision.lib;

import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTable;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.StringSubscriber;

/**
 * Coprocessor-wide controls over NetworkTables: match recording, the vision LED and the node graph.
 *
 * <p>Unlike {@link LumenCoprocessorControl} (HTTP, blocking), every call here is a non-blocking
 * NT4 publish or cached read, so it is safe from periodic robot code:
 *
 * <pre>{@code
 * LumenCoprocessor coprocessor = new LumenCoprocessor(NetworkTableInstance.getDefault());
 *
 * public void autonomousInit() { coprocessor.startRecording(); }
 * public void disabledInit()   { coprocessor.stopRecording(); }
 * }</pre>
 *
 * <p>Recording is desired state: the robot publishes {@code <rootTable>/config/recording} and the
 * coprocessor records every camera while it is true, resuming after a reboot. {@link
 * #isRecording()} reads the coprocessor-published {@code <rootTable>/status/recording}.
 */
public class LumenCoprocessor {
    /** The coprocessor's default NT root table (NetworkTablesConfig.rootTable). */
    public static final String DEFAULT_ROOT_TABLE = "lumenvision";

    /** The topic-layout version this library speaks; the coprocessor publishes its own as {@code .protocol}. */
    public static final int PROTOCOL_VERSION = 4;

    private final BooleanPublisher recordingRequestPub;
    private final BooleanSubscriber recordingStatusSub;
    private final IntegerPublisher ledModePub;
    private final IntegerSubscriber ledModeSub;
    private final StringSubscriber graphSub;
    private final IntegerSubscriber protocolSub;

    /** Uses the coprocessor's default root table, {@value #DEFAULT_ROOT_TABLE}. */
    public LumenCoprocessor(NetworkTableInstance instance) {
        this(instance, DEFAULT_ROOT_TABLE);
    }

    /**
     * @param instance the NetworkTableInstance to use (injectable for simulation)
     * @param rootTable must match the coprocessor's NetworkTablesConfig.rootTable
     */
    public LumenCoprocessor(NetworkTableInstance instance, String rootTable) {
        NetworkTable root = instance.getTable(rootTable);
        recordingRequestPub = root.getSubTable("config").getBooleanTopic("recording").publish();
        recordingStatusSub = root.getSubTable("status").getBooleanTopic("recording").subscribe(false);
        ledModePub = root.getSubTable("config").getIntegerTopic("ledMode").publish();
        ledModeSub = root.getSubTable("status").getIntegerTopic("ledMode").subscribe(-1);
        graphSub = root.getStringTopic(".graph").subscribe("");
        protocolSub = root.getIntegerTopic(".protocol").subscribe(-1);
    }

    /** Asks the coprocessor to start recording every camera. */
    public void startRecording() {
        setRecording(true);
    }

    /** Asks the coprocessor to stop recording (the current segment is finalised). */
    public void stopRecording() {
        setRecording(false);
    }

    /** Sets the desired recording state. */
    public void setRecording(boolean recording) {
        recordingRequestPub.set(recording);
    }

    /**
     * Whether the coprocessor reports that it is recording; false until it has published its status.
     */
    public boolean isRecording() {
        return recordingStatusSub.get();
    }

    /** Sets the coprocessor-wide vision LED mode. */
    public void setLed(LumenLedMode mode) {
        ledModePub.set(mode.value);
    }

    /** The LED mode the coprocessor reports applied. */
    public LumenLedMode getLed() {
        return LumenLedMode.fromValue((int) ledModeSub.get());
    }

    /** The coprocessor's node graph; empty until it has published one. */
    public LumenGraph getGraph() {
        return LumenGraph.parse(graphSub.get());
    }

    /** The topic-layout version the coprocessor reports, or -1 before it has published one. */
    public int getProtocol() {
        return (int) protocolSub.get();
    }
}
