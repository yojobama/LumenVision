package frc.robot.subsystems;

import edu.wpi.first.apriltag.AprilTagFieldLayout;
import edu.wpi.first.apriltag.AprilTagFields;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.math.util.Units;
import edu.wpi.first.wpilibj2.command.SubsystemBase;
import java.util.Optional;
import org.lumenvision.photoncompat.EstimatedRobotPose;
import org.lumenvision.photoncompat.PhotonCamera;
import org.lumenvision.photoncompat.PhotonPoseEstimator;
import org.lumenvision.photoncompat.PoseStrategy;

/**
 * The same vision loop written against photonlib's class and method names, which org.lumenvision.photoncompat provides on top of the
 * LumenVision API: swapping photonlib's imports for these is all a migrating team changes.
 */
public class PhotonCompatVisionSubsystem extends SubsystemBase {
  private static final Transform3d kRobotToCamera =
      new Transform3d(
          new Translation3d(Units.inchesToMeters(10), 0, Units.inchesToMeters(20)),
          new Rotation3d(0, Units.degreesToRadians(-15), 0));

  private final PhotonCamera m_camera = new PhotonCamera("front");
  private final PhotonPoseEstimator m_estimator =
      new PhotonPoseEstimator(
          AprilTagFieldLayout.loadField(AprilTagFields.kDefaultField),
          PoseStrategy.MULTI_TAG_PNP_ON_COPROCESSOR,
          kRobotToCamera);

  private Optional<EstimatedRobotPose> m_latest = Optional.empty();

  public PhotonCompatVisionSubsystem() {
    m_estimator.setMultiTagFallbackStrategy(PoseStrategy.LOWEST_AMBIGUITY);
  }

  /** Driver mode stops detection (the camera keeps streaming); switch it on for teleop driving by camera. */
  public void setDriverMode(boolean driverMode) {
    m_camera.setDriverMode(driverMode);
  }

  public void takeSnapshots() {
    m_camera.takeInputSnapshot();
    m_camera.takeOutputSnapshot();
  }

  public Optional<EstimatedRobotPose> getLatestEstimate() {
    return m_latest;
  }

  @Override
  public void periodic() {
    for (var result : m_camera.getAllUnreadResults()) {
      Optional<EstimatedRobotPose> estimate = m_estimator.update(result);
      if (estimate.isPresent()) m_latest = estimate;
    }
  }
}
