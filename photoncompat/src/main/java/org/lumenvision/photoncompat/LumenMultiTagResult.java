package org.lumenvision.photoncompat;

import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.math.numbers.N3;
import org.ejml.simple.SimpleMatrix;

/**
 * The coprocessor's multi-tag PnP result: one field-relative camera pose solved jointly from all
 * visible tags with known field poses. Analogous to photonlib's MultiTargetPNPResult, but the
 * coprocessor has already inverted it into a camera {@link Pose3d}.
 */
public class LumenMultiTagResult {
    private final Pose3d fieldToCamera;
    private final int tagCount;
    private final double reprojectionErrorPixels;

    /**
     * @param rotationRowMajor the 9 elements of the camera's field-relative rotation matrix, row-major
     *     (the raw matrix published as tags/r0..r8, as in {@link LumenTrackedTarget})
     */
    LumenMultiTagResult(double x, double y, double z, double[] rotationRowMajor, int tagCount, double reprojectionErrorPixels) {
        Matrix<N3, N3> rotationMatrix = new Matrix<>(new SimpleMatrix(3, 3, true, rotationRowMajor));
        this.fieldToCamera = new Pose3d(new Translation3d(x, y, z), new Rotation3d(rotationMatrix));
        this.tagCount = tagCount;
        this.reprojectionErrorPixels = reprojectionErrorPixels;
    }

    /**
     * The camera's pose in field coordinates (not camera-to-tag). Compose with the camera mount
     * offset via {@code Pose3d.transformBy(Transform3d cameraToRobot)} to get the robot pose.
     */
    public Pose3d getFieldToCamera() {
        return fieldToCamera;
    }

    /** Number of tags in the solve (always >= 2). */
    public int getTagCount() {
        return tagCount;
    }

    /** RMS reprojection error in pixels across the contributing tags' corners; a value well above ~1px
     * on a calibrated camera suggests noisy corners (blur, edge tags, occlusion). */
    public double getReprojectionErrorPixels() {
        return reprojectionErrorPixels;
    }
}
