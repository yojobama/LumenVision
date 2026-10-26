package org.lumenvision.photoncompat.sim;

import edu.wpi.first.math.geometry.Pose3d;

/** A target placed in the simulated field: a pose, a shape and (for AprilTags) the fiducial id the camera will report. */
public class VisionTargetSim {
    private Pose3d pose;
    private final TargetModel model;
    private final int fiducialId;

    /** A non-fiducial target (reported with fiducial id -1). */
    public VisionTargetSim(Pose3d pose, TargetModel model) {
        this(pose, model, -1);
    }

    public VisionTargetSim(Pose3d pose, TargetModel model, int fiducialId) {
        this.pose = pose;
        this.model = model;
        this.fiducialId = fiducialId;
    }

    public Pose3d getPose() {
        return pose;
    }

    public void setPose(Pose3d pose) {
        this.pose = pose;
    }

    public TargetModel getModel() {
        return model;
    }

    /** The AprilTag id, or -1 for any other target. */
    public int getFiducialId() {
        return fiducialId;
    }
}
