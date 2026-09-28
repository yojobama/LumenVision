package org.lumenvision.photoncompat;

import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.math.numbers.N3;
import org.ejml.simple.SimpleMatrix;

/**
 * One AprilTag detection, in the shape photonlib's PhotonTrackedTarget already has (same member
 * names) so a team migrating from PhotonVision changes an import line, not call sites.
 */
public class LumenTrackedTarget {
    private final int fiducialId;
    private final Transform3d bestCameraToTarget;

    /**
     * @param rotationRowMajor the 9 elements of the tag's 3x3 rotation matrix, row-major (published
     *     as tags/r0..r8); this constructor performs the conversion into WPILib geometry types
     */
    LumenTrackedTarget(int fiducialId, double x, double y, double z, double[] rotationRowMajor) {
        this.fiducialId = fiducialId;

        Matrix<N3, N3> rotationMatrix =
                new Matrix<>(new SimpleMatrix(3, 3, true, rotationRowMajor));
        this.bestCameraToTarget =
                new Transform3d(new Translation3d(x, y, z), new Rotation3d(rotationMatrix));
    }

    /** The decoded AprilTag ID. */
    public int getFiducialId() {
        return fiducialId;
    }

    /**
     * The camera-to-tag transform in the camera frame (X forward, Y left, Z up, matching
     * apriltag_pose.h and WPILib's {@link Rotation3d}).
     */
    public Transform3d getBestCameraToTarget() {
        return bestCameraToTarget;
    }
}
