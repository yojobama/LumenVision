package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Quaternion;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;

import java.nio.ByteBuffer;
import java.nio.BufferUnderflowException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Optional;

/**
 * Decoder for the binary {@code <camera>/result} packet (schema version 2) NetworkTablesSink publishes. The layout is specified in
 * LumenCore/ResultPacket.h and locked by tests/data/packet_v2.bin, which this module's tests decode too. All numbers are
 * big-endian; every pose is already in WPILib frames.
 */
final class LumenResultPacket {
    static final int SCHEMA_VERSION = 2;

    private LumenResultPacket() {}

    /** One decoded packet; {@link #targets} is sorted largest-area first, so the first is the best target. */
    static final class Decoded {
        final long sequenceId;
        final long latencyMicros;
        final Optional<LumenMultiTagResult> multiTag;
        final Optional<LumenConstrainedResult> constrained;
        final List<LumenTrackedTarget> targets;

        Decoded(long sequenceId, long latencyMicros, Optional<LumenMultiTagResult> multiTag, Optional<LumenConstrainedResult> constrained,
                List<LumenTrackedTarget> targets) {
            this.sequenceId = sequenceId;
            this.latencyMicros = latencyMicros;
            this.multiTag = multiTag;
            this.constrained = constrained;
            this.targets = targets;
        }
    }

    /** @return empty for a truncated packet or one from another schema version */
    static Optional<Decoded> decode(byte[] bytes) {
        if (bytes == null || bytes.length < 2) return Optional.empty();
        ByteBuffer in = ByteBuffer.wrap(bytes); // big-endian by default
        try {
            if ((in.getShort() & 0xFFFF) != SCHEMA_VERSION) return Optional.empty();
            long sequenceId = in.getLong();
            long latencyMicros = in.getInt() & 0xFFFFFFFFL;

            boolean hasMultiTag = in.get() != 0;
            Pose3d fieldToCamera = null;
            double multiTagReprojError = -1;
            if (hasMultiTag) {
                Translation3d translation = new Translation3d(in.getDouble(), in.getDouble(), in.getDouble());
                Quaternion q = new Quaternion(in.getDouble(), in.getDouble(), in.getDouble(), in.getDouble());
                fieldToCamera = new Pose3d(translation, new Rotation3d(q));
                multiTagReprojError = in.getFloat();
            }

            Optional<LumenConstrainedResult> constrained = Optional.empty();
            if (in.get() != 0) {
                double x = in.getDouble();
                double y = in.getDouble();
                double yaw = in.getDouble();
                double reprojError = in.getFloat();
                int tagCount = in.get() & 0xFF;
                constrained = Optional.of(new LumenConstrainedResult(new Pose2d(x, y, new Rotation2d(yaw)), reprojError, tagCount));
            }

            int idCount = in.get() & 0xFF;
            List<Integer> ids = new ArrayList<>(idCount);
            for (int i = 0; i < idCount; i++) ids.add(in.getShort() & 0xFFFF);

            int targetCount = in.getShort() & 0xFFFF;
            List<LumenTrackedTarget> targets = new ArrayList<>(targetCount);
            for (int i = 0; i < targetCount; i++) targets.add(readTarget(in));
            targets.sort(Comparator.comparingDouble(LumenTrackedTarget::getArea).reversed());

            Optional<LumenMultiTagResult> multiTag = hasMultiTag
                    ? Optional.of(new LumenMultiTagResult(fieldToCamera, ids, multiTagReprojError))
                    : Optional.empty();
            return Optional.of(new Decoded(sequenceId, latencyMicros, multiTag, constrained, targets));
        } catch (BufferUnderflowException e) {
            return Optional.empty();
        }
    }

    private static LumenTrackedTarget readTarget(ByteBuffer in) {
        int fiducialId = in.getShort();
        int objectClassId = in.getShort();
        float objectConfidence = in.getFloat();
        double yaw = in.getDouble();
        double pitch = in.getDouble();
        double area = in.getDouble();
        double skew = in.getDouble();
        double ambiguity = in.getDouble();
        Transform3d best = readTransform(in);
        Transform3d alternate = readTransform(in);
        float bestReprojError = in.getFloat();
        float altReprojError = in.getFloat();
        double[] corners = new double[8];
        for (int i = 0; i < 8; i++) corners[i] = in.getFloat();
        double[] minAreaRect = new double[8];
        for (int i = 0; i < 8; i++) minAreaRect[i] = in.getFloat();
        return new LumenTrackedTarget(fiducialId, objectClassId, objectConfidence, yaw, pitch, area, skew, ambiguity,
                best, alternate, bestReprojError, altReprojError, corners, minAreaRect);
    }

    private static Transform3d readTransform(ByteBuffer in) {
        Translation3d translation = new Translation3d(in.getFloat(), in.getFloat(), in.getFloat());
        Quaternion q = new Quaternion(in.getFloat(), in.getFloat(), in.getFloat(), in.getFloat());
        return new Transform3d(translation, new Rotation3d(q));
    }
}
