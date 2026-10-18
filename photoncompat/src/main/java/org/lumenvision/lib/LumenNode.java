package org.lumenvision.lib;

import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.NetworkTableInstance;

/**
 * A node of the coprocessor's graph, addressed by the name its topics are published under. Any node can be started and stopped and asked
 * to save its latest frame; {@link LumenDetector} adds results.
 *
 * <p>Writes go to {@code <rootTable>/<node>/config/*} and the state in effect is read back from {@code status/*}.
 */
public class LumenNode implements AutoCloseable {
    private final NetworkTableInstance instance;
    private final String rootTable;
    private final String name;

    private final BooleanPublisher enabledPub;
    private final IntegerPublisher snapshotPub;
    private final BooleanSubscriber enabledSub;
    private long snapshotCount;

    /**
     * @param instance the NetworkTableInstance to use (injectable for simulation)
     * @param rootTable must match the coprocessor's NetworkTablesConfig.rootTable (default "lumenvision")
     * @param name the node's name as published by the coprocessor (see {@link LumenGraph.Node#getName()}), or its id as text
     */
    public LumenNode(NetworkTableInstance instance, String rootTable, String name) {
        this.instance = instance;
        this.rootTable = rootTable;
        this.name = name;
        String base = "/" + rootTable + "/" + tableName(name) + "/";
        enabledPub = instance.getBooleanTopic(base + "config/enabled").publish();
        snapshotPub = instance.getIntegerTopic(base + "config/snapshot").publish();
        enabledSub = instance.getBooleanTopic(base + "status/enabled").subscribe(true);
    }

    /** The NT table name a node's topics are published under: its name with characters outside {@code [A-Za-z0-9_.-]} replaced by {@code _}. */
    public static String tableName(String nodeName) {
        StringBuilder out = new StringBuilder(nodeName.length());
        for (int i = 0; i < nodeName.length(); i++) {
            char c = nodeName.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            out.append(ok ? c : '_');
        }
        return out.toString();
    }

    public String getName() {
        return name;
    }

    /** The NetworkTables instance this node reads and writes (the simulation publishes into the same one). */
    public NetworkTableInstance getNetworkTableInstance() {
        return instance;
    }

    /** The NT root table, e.g. "lumenvision". */
    public String getRootTable() {
        return rootTable;
    }

    /** Starts or stops the node; the choice is kept across coprocessor restarts. */
    public void setEnabled(boolean enabled) {
        enabledPub.set(enabled);
    }

    /** Whether the coprocessor reports the node running; true until it has reported. */
    public boolean isEnabled() {
        return enabledSub.get();
    }

    /** Saves the node's latest frame on the coprocessor (the annotated frame for a detector, the raw one for a camera). */
    public void takeSnapshot() {
        snapshotPub.set(++snapshotCount);
    }

    @Override
    public void close() {
        for (AutoCloseable closeable : new AutoCloseable[] { enabledPub, snapshotPub, enabledSub }) {
            try {
                closeable.close();
            } catch (Exception ignored) {
                // closing a topic handle cannot fail in a way the caller could act on
            }
        }
    }
}
