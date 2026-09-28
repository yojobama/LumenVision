package org.lumenvision.photoncompat;

import edu.wpi.first.networktables.DoubleArraySubscriber;
import edu.wpi.first.networktables.DoubleArrayTopic;
import edu.wpi.first.networktables.DoubleSubscriber;
import edu.wpi.first.networktables.NetworkTable;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.StringSubscriber;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/**
 * Robot-side client for one LumenVision coprocessor camera, mirroring photonlib's PhotonCamera.
 *
 * <p>Reads the NT4 tag topics published by NetworkTablesSink under {@code
 * <rootTable>/<sourceName>/tags/*} and the {@code multitag/*} scalars ({@link #getMultiTagResult()}).
 */
public class LumenCamera {
    private final DoubleArraySubscriber idsSub;
    private final DoubleArraySubscriber xSub;
    private final DoubleArraySubscriber ySub;
    private final DoubleArraySubscriber zSub;
    private final DoubleArraySubscriber[] rSub = new DoubleArraySubscriber[9];

    private final DoubleSubscriber multiTagXSub;
    private final DoubleSubscriber multiTagYSub;
    private final DoubleSubscriber multiTagZSub;
    private final DoubleSubscriber[] multiTagRSub = new DoubleSubscriber[9];
    private final DoubleSubscriber multiTagTagCountSub;
    private final DoubleSubscriber multiTagReprojErrSub;

    private final StringSubscriber versionSub;
    // checked at most once; the coprocessor's ".version" is constant for the life of its process
    private boolean versionChecked = false;

    /**
     * @param instance the NetworkTableInstance to read from (injectable for simulation)
     * @param rootTable must match the coprocessor's NetworkTablesConfig.rootTable (default "lumenvision")
     * @param sourceName must match the bound source's ID, which is the subtable name under {@code rootTable}
     */
    public LumenCamera(NetworkTableInstance instance, String rootTable, String sourceName) {
        NetworkTable sourceTable = instance.getTable(rootTable + "/" + sourceName);
        NetworkTable tagsTable = sourceTable.getSubTable("tags");
        NetworkTable multiTagTable = sourceTable.getSubTable("multitag");

        idsSub = subscribeArray(tagsTable, "ids");
        xSub = subscribeArray(tagsTable, "x");
        ySub = subscribeArray(tagsTable, "y");
        zSub = subscribeArray(tagsTable, "z");
        for (int i = 0; i < 9; i++) {
            rSub[i] = subscribeArray(tagsTable, "r" + i);
        }

        multiTagXSub = subscribeScalar(multiTagTable, "x");
        multiTagYSub = subscribeScalar(multiTagTable, "y");
        multiTagZSub = subscribeScalar(multiTagTable, "z");
        for (int i = 0; i < 9; i++) {
            multiTagRSub[i] = subscribeScalar(multiTagTable, "r" + i);
        }
        multiTagTagCountSub = subscribeScalar(multiTagTable, "tagCount");
        multiTagReprojErrSub = subscribeScalar(multiTagTable, "reprojErrPixels");

        versionSub = instance.getTable(rootTable).getStringTopic(".version").subscribe("");
    }

    private static DoubleArraySubscriber subscribeArray(NetworkTable table, String name) {
        DoubleArrayTopic topic = table.getDoubleArrayTopic(name);
        return topic.subscribe(new double[0]);
    }

    private static DoubleSubscriber subscribeScalar(NetworkTable table, String name) {
        return table.getDoubleTopic(name).subscribe(0.0);
    }

    /**
     * The most recent detection set, decoded into WPILib geometry types. Never null; empty
     * targets means no tags this frame (or no frame yet).
     */
    public LumenPipelineResult getLatestResult() {
        // deferred from the constructor: the NT4 handshake may not have completed yet, so ".version" would read empty
        if (!versionChecked) {
            String coprocessorVersion = versionSub.get();
            if (!coprocessorVersion.isEmpty()) {
                LumenVersionCheck.warnOnMismatch(coprocessorVersion);
                versionChecked = true;
            }
        }

        double[] ids = idsSub.get();
        double[] x = xSub.get();
        double[] y = ySub.get();
        double[] z = zSub.get();
        double[][] r = new double[9][];
        for (int i = 0; i < 9; i++) {
            r[i] = rSub[i].get();
        }

        List<LumenTrackedTarget> targets = new ArrayList<>(ids.length);
        for (int i = 0; i < ids.length; i++) {
            // guards against a torn read: the parallel arrays are published together but NT4 delivers each topic separately
            if (i >= x.length || i >= y.length || i >= z.length) break;
            double[] rotationRowMajor = new double[9];
            boolean rotationComplete = true;
            for (int j = 0; j < 9; j++) {
                if (i >= r[j].length) {
                    rotationComplete = false;
                    break;
                }
                rotationRowMajor[j] = r[j][i];
            }
            if (!rotationComplete) break;

            targets.add(new LumenTrackedTarget((int) ids[i], x[i], y[i], z[i], rotationRowMajor));
        }

        // NT4 subscriber timestamps are already in the local clock domain; microseconds -> seconds
        // as addVisionMeasurement expects
        double timestampSeconds = idsSub.getLastChange() / 1_000_000.0;

        // read with the same snapshot as targets/timestamp so LumenPoseEstimator sees one coherent frame
        return new LumenPipelineResult(targets, timestampSeconds, getMultiTagResult());
    }

    /**
     * The coprocessor's multi-tag PnP result, if published this frame. Empty when {@code
     * multitag/tagCount} is 0 (fewer than 2 visible tags have known field poses).
     */
    public Optional<LumenMultiTagResult> getMultiTagResult() {
        int tagCount = (int) multiTagTagCountSub.get();
        if (tagCount < 2) return Optional.empty();

        double[] rotationRowMajor = new double[9];
        for (int i = 0; i < 9; i++) {
            rotationRowMajor[i] = multiTagRSub[i].get();
        }

        return Optional.of(new LumenMultiTagResult(
                multiTagXSub.get(), multiTagYSub.get(), multiTagZSub.get(),
                rotationRowMajor, tagCount, multiTagReprojErrSub.get()));
    }
}
