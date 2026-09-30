package org.lumenvision.photoncompat.sim;

import edu.wpi.first.apriltag.AprilTag;
import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Transform3d;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * The simulated world for a robot's cameras: the field's vision targets, the robot's pose and each camera's mount. Call
 * {@link #update(Pose2d)} every simulation loop with the simulated robot pose and every camera publishes what it would see.
 */
public class VisionSystemSim {
    private final String name;
    private final Map<PhotonCameraSim, Transform3d> cameras = new LinkedHashMap<>();
    private final List<VisionTargetSim> aprilTags = new ArrayList<>();
    private final List<VisionTargetSim> otherTargets = new ArrayList<>();
    private Pose3d layoutOrigin = new Pose3d();
    private Pose3d robotPose = new Pose3d();

    public VisionSystemSim(String visionSystemName) {
        this.name = visionSystemName;
    }

    public String getName() {
        return name;
    }

    // ---- cameras ----

    public void addCamera(PhotonCameraSim cameraSim, Transform3d robotToCamera) {
        cameraSim.setLayoutOrigin(layoutOrigin);
        cameras.put(cameraSim, robotToCamera);
    }

    public boolean removeCamera(PhotonCameraSim cameraSim) {
        return cameras.remove(cameraSim) != null;
    }

    public Collection<PhotonCameraSim> getCameraSims() {
        return new ArrayList<>(cameras.keySet());
    }

    /** Moves a camera on the robot; returns false when the camera was never added. */
    public boolean adjustCamera(PhotonCameraSim cameraSim, Transform3d robotToCamera) {
        if (!cameras.containsKey(cameraSim)) return false;
        cameras.put(cameraSim, robotToCamera);
        return true;
    }

    public Optional<Transform3d> getRobotToCamera(PhotonCameraSim cameraSim) {
        return Optional.ofNullable(cameras.get(cameraSim));
    }

    /** A camera's field pose as of the last {@link #update}. */
    public Optional<Pose3d> getCameraPose(PhotonCameraSim cameraSim) {
        Transform3d mount = cameras.get(cameraSim);
        return mount == null ? Optional.empty() : Optional.of(robotPose.transformBy(mount));
    }

    // ---- targets ----

    /** Adds every tag in the layout as a 6.5 inch 36h11 tag, and tells each camera which frame the coprocessor solves multi-tag poses in. */
    public void addAprilTags(AprilTagFieldLayout layout) {
        addAprilTags(layout, TargetModel.kAprilTag36h11);
    }

    public void addAprilTags(AprilTagFieldLayout layout, TargetModel tagModel) {
        for (AprilTag tag : layout.getTags()) {
            aprilTags.add(new VisionTargetSim(layout.getTagPose(tag.ID).orElse(tag.pose), tagModel, tag.ID));
        }
        layoutOrigin = layout.getOrigin();
        for (PhotonCameraSim camera : cameras.keySet()) camera.setLayoutOrigin(layoutOrigin);
    }

    public void clearAprilTags() {
        aprilTags.clear();
    }

    public void addVisionTargets(VisionTargetSim... targets) {
        for (VisionTargetSim target : targets) {
            (target.getFiducialId() >= 0 ? aprilTags : otherTargets).add(target);
        }
    }

    public void clearVisionTargets() {
        otherTargets.clear();
    }

    public void removeVisionTargets(VisionTargetSim... targets) {
        for (VisionTargetSim target : targets) {
            aprilTags.remove(target);
            otherTargets.remove(target);
        }
    }

    public Collection<VisionTargetSim> getVisionTargets() {
        List<VisionTargetSim> all = new ArrayList<>(aprilTags);
        all.addAll(otherTargets);
        return all;
    }

    // ---- running ----

    public void update(Pose2d robotPoseMeters) {
        update(new Pose3d(robotPoseMeters));
    }

    /** Moves the robot and lets every camera capture (and publish) a frame if its frame interval has elapsed. */
    public void update(Pose3d robotPoseMeters) {
        this.robotPose = robotPoseMeters;
        Collection<VisionTargetSim> targets = getVisionTargets();
        for (Map.Entry<PhotonCameraSim, Transform3d> camera : cameras.entrySet()) {
            camera.getKey().update(robotPose.transformBy(camera.getValue()), targets);
        }
    }

    public Pose3d getRobotPose() {
        return robotPose;
    }

    public void resetRobotPose(Pose2d pose) {
        robotPose = new Pose3d(pose);
    }

    public void resetRobotPose(Pose3d pose) {
        robotPose = pose;
    }
}
