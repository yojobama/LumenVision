package frc.robot.subsystems;

import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.apriltag.AprilTagFields;
import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.math.util.Units;
import edu.wpi.first.wpilibj.smartdashboard.Field2d;
import edu.wpi.first.wpilibj.smartdashboard.SmartDashboard;
import edu.wpi.first.wpilibj2.command.SubsystemBase;
import java.util.Optional;
import org.lumenvision.lib.LumenDetector;
import org.lumenvision.lib.LumenPoseEstimator;
import org.lumenvision.lib.LumenRobotPose;
import org.lumenvision.photoncompat.sim.PhotonCameraSim;
import org.lumenvision.photoncompat.sim.SimCameraProperties;
import org.lumenvision.photoncompat.sim.VisionSystemSim;

/**
 * Reads a LumenVision detector node and estimates the robot's field pose from it. In simulation a simulated camera stands in for the
 * coprocessor, publishing to the same NetworkTables topics, so this code runs unchanged.
 */
public class VisionSubsystem extends SubsystemBase {
  // the detector node's name in the LumenVision web UI, and where its camera sits on the robot
  private static final String kDetectorName = "front";
  private static final Transform3d kRobotToCamera =
      new Transform3d(
          new Translation3d(Units.inchesToMeters(10), 0, Units.inchesToMeters(20)),
          new Rotation3d(0, Units.degreesToRadians(-15), 0));

  private final AprilTagFieldLayout m_layout = AprilTagFieldLayout.loadField(AprilTagFields.kDefaultField);
  private final LumenDetector m_detector = new LumenDetector(kDetectorName);
  private final LumenPoseEstimator m_estimator = new LumenPoseEstimator(m_layout, kRobotToCamera);
  private final Field2d m_field = new Field2d();

  private VisionSystemSim m_visionSim;

  private Optional<LumenRobotPose> m_latest = Optional.empty();

  public VisionSubsystem() {
    SmartDashboard.putData("VisionPose", m_field);
  }

  /** The latest estimate, stamped with the frame's capture time so it can be fed to a pose estimator. */
  public Optional<LumenRobotPose> getLatestEstimate() {
    return m_latest;
  }

  @Override
  public void periodic() {
    for (var result : m_detector.getAllUnreadResults()) {
      Optional<LumenRobotPose> estimate = m_estimator.estimateMultiTag(result);
      if (estimate.isPresent()) {
        m_latest = estimate;
        m_field.setRobotPose(estimate.get().getPose().toPose2d());
        // drivetrain.addVisionMeasurement(estimate.get().getPose().toPose2d(), estimate.get().getTimestampSeconds());
      }
    }
  }

  /** Simulates the camera seeing the field's tags from the robot's simulated pose. */
  public void simulate(Pose2d simulatedRobotPose) {
    if (m_visionSim == null) {
      m_visionSim = new VisionSystemSim("main");
      m_visionSim.addAprilTags(m_layout);
      m_visionSim.addCamera(new PhotonCameraSim(m_detector, SimCameraProperties.OV9281_1280_800()), kRobotToCamera);
    }
    m_visionSim.update(simulatedRobotPose);
  }
}
