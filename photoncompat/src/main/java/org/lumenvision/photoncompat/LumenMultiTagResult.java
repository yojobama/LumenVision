package org.lumenvision.photoncompat;

import edu.wpi.first.math.geometry.Pose3d;

import java.util.Collections;
import java.util.List;

/**
 * The coprocessor's multi-tag PnP result: one field-relative camera pose solved jointly from all visible tags with known field
 * poses. Analogous to photonlib's MultiTargetPNPResult, but the coprocessor has already inverted it into a camera {@link Pose3d}.
 */
public class LumenMultiTagResult {
    private final Pose3d fieldToCamera;
    private final List<Integer> fiducialIds;
    private final double reprojectionErrorPixels;

    LumenMultiTagResult(Pose3d fieldToCamera, List<Integer> fiducialIds, double reprojectionErrorPixels) {
        this.fieldToCamera = fieldToCamera;
        this.fiducialIds = Collections.unmodifiableList(fiducialIds);
        this.reprojectionErrorPixels = reprojectionErrorPixels;
    }

    /**
     * The camera's pose in the coprocessor's field-layout frame, camera axes X forward / Y left / Z up (not camera-to-tag). Compose
     * with the camera mount offset via {@code transformBy(robotToCamera.inverse())} to get the robot pose.
     */
    public Pose3d getFieldToCamera() {
        return fieldToCamera;
    }

    /** The AprilTag ids used in the solve. */
    public List<Integer> getFiducialIds() {
        return fiducialIds;
    }

    /** Number of tags in the solve (always &gt;= 2). */
    public int getTagCount() {
        return fiducialIds.size();
    }

    /** RMS reprojection error in pixels across the contributing tags' corners; well above ~1px on a calibrated camera suggests noisy corners. */
    public double getReprojectionErrorPixels() {
        return reprojectionErrorPixels;
    }
}
