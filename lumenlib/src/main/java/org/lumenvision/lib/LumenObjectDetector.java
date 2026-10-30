package org.lumenvision.lib;

import edu.wpi.first.networktables.NetworkTableInstance;

/**
 * An object-detection node of the coprocessor's graph. Each result has the objects the model found in a frame, with their class,
 * confidence and bounding box; yaw and pitch to each need the camera's calibration, as does {@link #getCameraMatrix()} for ranging.
 *
 * <pre>{@code
 * LumenObjectDetector intake = new LumenObjectDetector("intake");
 * LumenObjectDetectionResult result = intake.getLatestResult();
 * for (LumenDetectedObject piece : result.getTargetsOfClass(GAME_PIECE_CLASS)) {
 *     double yawDegrees = piece.getYaw();
 *     double widthPixels = piece.getBoxWidthPixels();
 * }
 * }</pre>
 */
public class LumenObjectDetector extends LumenDetector<LumenObjectDetectionResult> {
    /**
     * The default NT4 instance and root table "lumenvision".
     *
     * @param name the node's name in the LumenVision UI (or the NT table its topics are under)
     */
    public LumenObjectDetector(String name) {
        this(NetworkTableInstance.getDefault(), LumenCoprocessor.DEFAULT_ROOT_TABLE, name);
    }

    /**
     * @param instance the NetworkTableInstance to use (injectable for simulation)
     * @param rootTable must match the coprocessor's NetworkTablesConfig.rootTable (default "lumenvision")
     * @param name the node's name in the LumenVision UI (or the NT table its topics are under)
     */
    public LumenObjectDetector(NetworkTableInstance instance, String rootTable, String name) {
        super(instance, rootTable, name, LumenGraph.OBJECT_DETECTOR);
    }

    @Override
    protected LumenObjectDetectionResult toResult(LumenPipelineResult packet) {
        return LumenObjectDetectionResult.from(packet);
    }
}
