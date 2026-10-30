package org.lumenvision.lib;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * What every detection has, whatever found it: where it is in the image and when the frame was captured. {@link LumenAprilTagTarget}
 * and {@link LumenDetectedObject} add what their kind of detector solves; {@link LumenTrackedTarget} is the packet's combined form.
 */
public abstract class LumenTarget {
    private final double yaw;
    private final double pitch;
    private final double area;
    private final double skew;
    private final List<LumenTargetCorner> detectedCorners;
    private final List<LumenTargetCorner> minAreaRectCorners;
    private final double timestampSeconds;
    private final long latencyMicros;

    /**
     * @param corners the detected corners as x0,y0..x3,y3 in image pixels
     * @param minAreaRectCorners the minimum-area rectangle around them, in the same form
     * @param timestampSeconds the frame's capture time (see {@link #getTimestampSeconds()})
     * @param latencyMicros its capture-to-publish latency
     */
    protected LumenTarget(double yaw, double pitch, double area, double skew, double[] corners, double[] minAreaRectCorners,
            double timestampSeconds, long latencyMicros) {
        this.yaw = yaw;
        this.pitch = pitch;
        this.area = area;
        this.skew = skew;
        this.detectedCorners = toCorners(corners);
        this.minAreaRectCorners = toCorners(minAreaRectCorners);
        this.timestampSeconds = timestampSeconds;
        this.latencyMicros = latencyMicros;
    }

    // the same target in another form, sharing its (immutable) corner lists
    LumenTarget(LumenTarget other) {
        this.yaw = other.yaw;
        this.pitch = other.pitch;
        this.area = other.area;
        this.skew = other.skew;
        this.detectedCorners = other.detectedCorners;
        this.minAreaRectCorners = other.minAreaRectCorners;
        this.timestampSeconds = other.timestampSeconds;
        this.latencyMicros = other.latencyMicros;
    }

    private static List<LumenTargetCorner> toCorners(double[] xy) {
        List<LumenTargetCorner> out = new ArrayList<>(xy.length / 2);
        for (int i = 0; i + 1 < xy.length; i += 2) out.add(new LumenTargetCorner(xy[i], xy[i + 1]));
        return Collections.unmodifiableList(out);
    }

    /** Horizontal angle to the target in degrees, positive to the left. With a calibration it is the target's centre; without one 0. */
    public double getYaw() {
        return yaw;
    }

    /** Vertical angle to the target in degrees, positive up. */
    public double getPitch() {
        return pitch;
    }

    /** Percentage (0 to 100) of the image the target's bounding quad covers; 0 without a calibration (the frame size is unknown). */
    public double getArea() {
        return area;
    }

    /** Rotation of the target's minimum-area rectangle in degrees, as OpenCV reports it. */
    public double getSkew() {
        return skew;
    }

    /** The detected corners in image pixels (an AprilTag's four, or an object's bounding box). */
    public List<LumenTargetCorner> getDetectedCorners() {
        return detectedCorners;
    }

    /** The minimum-area rectangle around the detected corners, in image pixels. */
    public List<LumenTargetCorner> getMinAreaRectCorners() {
        return minAreaRectCorners;
    }

    /**
     * When the frame this came from was captured, in seconds on the NetworkTables server clock (the roboRIO's FPGA time): the value
     * {@code addVisionMeasurement} wants. Same as the result it came with; 0 for one built without a time.
     */
    public double getTimestampSeconds() {
        return timestampSeconds;
    }

    /** Capture-to-publish latency of that frame in milliseconds; 0 for one built without a time. */
    public double getLatencyMillis() {
        return latencyMicros / 1000.0;
    }
}
