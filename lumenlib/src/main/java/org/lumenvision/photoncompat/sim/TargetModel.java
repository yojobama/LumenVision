package org.lumenvision.photoncompat.sim;

import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.math.util.Units;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The shape of a simulated vision target: its vertices in the target's own frame (X out of the face, Y left, Z up, the frame a
 * field-layout tag pose describes). A planar model has exactly four vertices in the order the coprocessor reports tag corners
 * (bottom-left, bottom-right, top-right, top-left seen from the front); a solid model has any number.
 */
public class TargetModel {
    /** A 6.5 inch 36h11 tag, the size used on the FRC field since 2024. */
    public static final TargetModel kAprilTag36h11 = new TargetModel(Units.inchesToMeters(6.5), Units.inchesToMeters(6.5));
    /** A 6 inch 16h5 tag. */
    public static final TargetModel kAprilTag16h5 = new TargetModel(Units.inchesToMeters(6.0), Units.inchesToMeters(6.0));

    private final List<Translation3d> vertices;
    private final boolean planar;

    /** A flat rectangle facing +X, {@code widthMeters} along Y and {@code heightMeters} along Z. */
    public TargetModel(double widthMeters, double heightMeters) {
        double y = widthMeters / 2.0;
        double z = heightMeters / 2.0;
        this.vertices = List.of(new Translation3d(0, -y, -z), new Translation3d(0, y, -z), new Translation3d(0, y, z), new Translation3d(0, -y, z));
        this.planar = true;
    }

    /** A solid with these vertices (centred on the target pose), reported as one bounding box. */
    public TargetModel(List<Translation3d> vertices) {
        if (vertices.isEmpty()) throw new IllegalArgumentException("a target model needs at least one vertex");
        this.vertices = Collections.unmodifiableList(new ArrayList<>(vertices));
        this.planar = false;
    }

    /** A box {@code lengthMeters} along X, {@code widthMeters} along Y and {@code heightMeters} along Z, centred on the target pose. */
    public static TargetModel cuboid(double lengthMeters, double widthMeters, double heightMeters) {
        List<Translation3d> corners = new ArrayList<>();
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sz = -1; sz <= 1; sz += 2)
                    corners.add(new Translation3d(sx * lengthMeters / 2.0, sy * widthMeters / 2.0, sz * heightMeters / 2.0));
        return new TargetModel(corners);
    }

    public boolean isPlanar() {
        return planar;
    }

    /** The vertices in the target's frame. */
    public List<Translation3d> getVertices() {
        return vertices;
    }

    /** The vertices in the field (or whatever frame {@code targetPose} is in). */
    public List<Translation3d> getFieldVertices(Pose3d targetPose) {
        List<Translation3d> out = new ArrayList<>(vertices.size());
        for (Translation3d vertex : vertices) out.add(targetPose.getTranslation().plus(vertex.rotateBy(targetPose.getRotation())));
        return out;
    }
}
