package org.lumenvision.photoncompat.sim;

import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Quaternion;
import edu.wpi.first.math.geometry.Transform3d;

import java.nio.ByteBuffer;
import java.util.List;

/**
 * Encoder for the binary result packet, schema version 2: the inverse of {@code LumenResultPacket}'s decoder, so a simulated camera
 * publishes bytes indistinguishable from the coprocessor's. The layout is documented in LumenCore/ResultPacket.h.
 */
final class SimResultEncoder {
    static final int SCHEMA_VERSION = 2;

    private SimResultEncoder() {}

    /** The camera's field pose (in the layout's frame) from a multi-tag solve. */
    record MultiTag(Pose3d cameraInLayout, double reprojErrorPixels) {}

    /** The robot's floor pose (in the layout's frame) from the constrained solve. */
    record Constrained(double x, double y, double yawRadians, double reprojErrorPixels, int tagCount) {}

    /** One detected target; angles in degrees, corners as x0,y0..x3,y3 pixels. */
    record Target(int fiducialId, double yaw, double pitch, double area, double skew, Transform3d cameraToTarget,
            double[] corners, double[] minAreaRect) {}

    static byte[] encode(long sequenceId, long latencyMicros, MultiTag multiTag, List<Integer> multiTagIds, Constrained constrained,
            List<Target> targets) {
        ByteBuffer out = ByteBuffer.allocate(2 + 8 + 4 + 1 + 3 * 8 + 4 * 8 + 4 + 1 + 3 * 8 + 4 + 1 + 1 + 2 * multiTagIds.size() + 2
                + targets.size() * targetSize());
        out.putShort((short) SCHEMA_VERSION);
        out.putLong(sequenceId);
        out.putInt((int) Math.min(latencyMicros, 0xFFFFFFFFL));

        if (multiTag != null) {
            out.put((byte) 1);
            out.putDouble(multiTag.cameraInLayout().getX());
            out.putDouble(multiTag.cameraInLayout().getY());
            out.putDouble(multiTag.cameraInLayout().getZ());
            Quaternion q = multiTag.cameraInLayout().getRotation().getQuaternion();
            out.putDouble(q.getW());
            out.putDouble(q.getX());
            out.putDouble(q.getY());
            out.putDouble(q.getZ());
            out.putFloat((float) multiTag.reprojErrorPixels());
        } else {
            out.put((byte) 0);
        }

        if (constrained != null) {
            out.put((byte) 1);
            out.putDouble(constrained.x());
            out.putDouble(constrained.y());
            out.putDouble(constrained.yawRadians());
            out.putFloat((float) constrained.reprojErrorPixels());
            out.put((byte) constrained.tagCount());
        } else {
            out.put((byte) 0);
        }

        out.put((byte) multiTagIds.size());
        for (int id : multiTagIds) out.putShort((short) id);

        out.putShort((short) targets.size());
        for (Target target : targets) {
            out.putShort((short) target.fiducialId());
            out.putShort((short) -1);
            out.putFloat(-1.0f);
            out.putDouble(target.yaw());
            out.putDouble(target.pitch());
            out.putDouble(target.area());
            out.putDouble(target.skew());
            out.putDouble(0.0); // ground-truth pose: no ambiguity
            putTransform(out, target.cameraToTarget());
            putTransform(out, target.cameraToTarget());
            out.putFloat(0.0f);
            out.putFloat(0.0f);
            for (double corner : target.corners()) out.putFloat((float) corner);
            for (double corner : target.minAreaRect()) out.putFloat((float) corner);
        }

        byte[] packet = new byte[out.position()];
        System.arraycopy(out.array(), 0, packet, 0, packet.length);
        return packet;
    }

    // fiducialId, objectClassId, confidence, 5 doubles, 2 transforms of 7 floats, 2 reprojection errors, 16 corner floats
    private static int targetSize() {
        return 2 + 2 + 4 + 5 * 8 + 2 * 7 * 4 + 2 * 4 + 16 * 4;
    }

    private static void putTransform(ByteBuffer out, Transform3d transform) {
        out.putFloat((float) transform.getX());
        out.putFloat((float) transform.getY());
        out.putFloat((float) transform.getZ());
        Quaternion q = transform.getRotation().getQuaternion();
        out.putFloat((float) q.getW());
        out.putFloat((float) q.getX());
        out.putFloat((float) q.getY());
        out.putFloat((float) q.getZ());
    }
}
