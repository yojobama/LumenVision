package org.lumenvision.photoncompat;

import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.NetworkTable;
import edu.wpi.first.networktables.NetworkTableInstance;

/**
 * Coprocessor-wide controls over NetworkTables - currently match recording.
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

    private final BooleanPublisher recordingRequestPub;
    private final BooleanSubscriber recordingStatusSub;

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
}
