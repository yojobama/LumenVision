package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import org.junit.jupiter.api.Test;

// Decodes tests/data/packet_v2.bin, the file LumenCore's test_result_packet writes; if the C++ layout and this decoder drift apart,
// one of the two fails.
class LumenResultPacketTest {
    private static byte[] golden() throws IOException {
        Path file = Paths.get("..", "tests", "data", "packet_v2.bin");
        return Files.readAllBytes(file);
    }

    @Test
    void decodesTheGoldenHeader() throws IOException {
        LumenResultPacket.Decoded packet = LumenResultPacket.decode(golden()).orElseThrow();

        assertEquals(42, packet.sequenceId);
        assertEquals(12345, packet.latencyMicros);
        assertTrue(packet.multiTag.isPresent());
        LumenMultiTagResult multiTag = packet.multiTag.get();
        assertEquals(List.of(3, 7), multiTag.getFiducialIds());
        assertEquals(2, multiTag.getTagCount());
        assertEquals(0.75, multiTag.getReprojectionErrorPixels(), 1e-6);
        assertEquals(1.5, multiTag.getFieldToCamera().getX(), 1e-9);
        assertEquals(-2.25, multiTag.getFieldToCamera().getY(), 1e-9);
        assertEquals(0.5, multiTag.getFieldToCamera().getZ(), 1e-9);
        assertEquals(-0.5, multiTag.getFieldToCamera().getRotation().getQuaternion().getZ(), 1e-9);
    }

    @Test
    void decodesTheGoldenConstrainedResult() throws IOException {
        LumenConstrainedResult constrained = LumenResultPacket.decode(golden()).orElseThrow().constrained.orElseThrow();

        assertEquals(3.25, constrained.getRobotPoseInLayoutFrame().getX(), 1e-12);
        assertEquals(-1.5, constrained.getRobotPoseInLayoutFrame().getY(), 1e-12);
        assertEquals(0.5, constrained.getRobotPoseInLayoutFrame().getRotation().getRadians(), 1e-12);
        assertEquals(0.25, constrained.getReprojectionErrorPixels(), 1e-6);
        assertEquals(2, constrained.getTagCount());
    }

    @Test
    void decodesTheGoldenTargetsLargestAreaFirst() throws IOException {
        List<LumenTrackedTarget> targets = LumenResultPacket.decode(golden()).orElseThrow().targets;
        assertEquals(2, targets.size());

        // the object detection (area 6.25) outranks the tag (area 1.75)
        LumenTrackedTarget object = targets.get(0);
        assertEquals(-1, object.getFiducialId());
        assertEquals(5, object.getDetectedObjectClassID());
        assertEquals(0.875f, object.getDetectedObjectConfidence(), 1e-6);
        assertEquals(3.0, object.getYaw(), 1e-9);
        assertEquals(-1.0, object.getPitch(), 1e-9);
        assertEquals(6.25, object.getArea(), 1e-9);
        assertEquals(-1.0, object.getPoseAmbiguity(), 1e-9);
        assertEquals(4, object.getDetectedCorners().size());
        assertEquals(110.0, object.getDetectedCorners().get(1).x, 1e-6);

        LumenTrackedTarget tag = targets.get(1);
        assertEquals(3, tag.getFiducialId());
        assertEquals(-1, tag.getDetectedObjectClassID());
        assertEquals(10.5, tag.getYaw(), 1e-9);
        assertEquals(-4.25, tag.getPitch(), 1e-9);
        assertEquals(-12.5, tag.getSkew(), 1e-9);
        assertEquals(0.125, tag.getPoseAmbiguity(), 1e-9);
        assertEquals(0.5, tag.getBestCameraToTarget().getX(), 1e-6);
        assertEquals(-0.25, tag.getBestCameraToTarget().getY(), 1e-6);
        assertEquals(2.0, tag.getBestCameraToTarget().getZ(), 1e-6);
        assertEquals(0.75, tag.getAlternateCameraToTarget().getX(), 1e-6);
        assertEquals(0.25, tag.getBestReprojError(), 1e-6);
        assertEquals(0.5, tag.getAltReprojError(), 1e-6);
        assertEquals(100.0, tag.getDetectedCorners().get(0).x, 1e-6);
        assertEquals(301.0, tag.getMinAreaRectCorners().get(1).x, 1e-6);
    }

    @Test
    void rejectsTruncatedAndForeignPackets() throws IOException {
        byte[] packet = golden();

        assertEquals(Optional.empty(), LumenResultPacket.decode(Arrays.copyOf(packet, packet.length - 1)));
        assertEquals(Optional.empty(), LumenResultPacket.decode(new byte[0]));
        assertEquals(Optional.empty(), LumenResultPacket.decode(null));

        byte[] foreign = packet.clone();
        foreign[1] = 9; // schema version 9
        assertEquals(Optional.empty(), LumenResultPacket.decode(foreign));
    }
}
