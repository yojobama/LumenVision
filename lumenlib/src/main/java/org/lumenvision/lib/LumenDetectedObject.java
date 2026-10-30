package org.lumenvision.lib;

import java.util.List;

/**
 * An object found by a {@link LumenObjectDetector}: the model's class and confidence and its bounding box in the image. Yaw and pitch are
 * to the box's centre and need the camera's calibration; the box is in pixels and needs none.
 */
public class LumenDetectedObject extends LumenTarget {
    private final int classId;
    private final float confidence;

    /**
     * @param corners the bounding box's corners as x0,y0..x3,y3 in image pixels: top left, top right, bottom right, bottom left
     * @param timestampSeconds the frame's capture time (see {@link #getTimestampSeconds()}); 0 for none, e.g. in a test
     * @param latencyMicros its capture-to-publish latency
     */
    public LumenDetectedObject(int classId, float confidence, double yaw, double pitch, double area, double[] corners,
            double timestampSeconds, long latencyMicros) {
        super(yaw, pitch, area, 0, corners, corners, timestampSeconds, latencyMicros);
        this.classId = classId;
        this.confidence = confidence;
    }

    LumenDetectedObject(LumenTrackedTarget packet) {
        super(packet);
        this.classId = packet.getDetectedObjectClassID();
        this.confidence = packet.getDetectedObjectConfidence();
    }

    /** The model's class id for the object. */
    public int getClassId() {
        return classId;
    }

    /** The model's confidence in the detection, 0 to 1. */
    public float getConfidence() {
        return confidence;
    }

    /**
     * The bounding box's corners in image pixels (x right, y down): top left, top right, bottom right, bottom left. Same as
     * {@link #getDetectedCorners()}.
     */
    public List<LumenTargetCorner> getBoundingBox() {
        return getDetectedCorners();
    }

    /** The bounding box's left edge in image pixels; 0 when the box is missing. */
    public double getBoxX() {
        return minCoordinate(true);
    }

    /** The bounding box's top edge in image pixels (y down); 0 when the box is missing. */
    public double getBoxY() {
        return minCoordinate(false);
    }

    /** The bounding box's width in image pixels; 0 when the box is missing. */
    public double getBoxWidthPixels() {
        return maxCoordinate(true) - minCoordinate(true);
    }

    /** The bounding box's height in image pixels; 0 when the box is missing. */
    public double getBoxHeightPixels() {
        return maxCoordinate(false) - minCoordinate(false);
    }

    // over the corners rather than by index, so a box whose corners come in another order still measures right
    private double minCoordinate(boolean x) {
        if (getDetectedCorners().isEmpty()) return 0;
        double min = Double.POSITIVE_INFINITY;
        for (LumenTargetCorner c : getDetectedCorners()) min = Math.min(min, x ? c.x : c.y);
        return min;
    }

    private double maxCoordinate(boolean x) {
        if (getDetectedCorners().isEmpty()) return 0;
        double max = Double.NEGATIVE_INFINITY;
        for (LumenTargetCorner c : getDetectedCorners()) max = Math.max(max, x ? c.x : c.y);
        return max;
    }
}
