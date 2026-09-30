package org.lumenvision.photoncompat.sim;

import edu.wpi.first.math.geometry.Pose2d;
import edu.wpi.first.math.geometry.Pose3d;
import edu.wpi.first.math.geometry.Quaternion;
import edu.wpi.first.math.geometry.Rotation3d;
import edu.wpi.first.math.geometry.Transform3d;
import edu.wpi.first.math.geometry.Translation3d;
import edu.wpi.first.networktables.BooleanPublisher;
import edu.wpi.first.networktables.BooleanSubscriber;
import edu.wpi.first.networktables.DoubleArrayPublisher;
import edu.wpi.first.networktables.DoubleArraySubscriber;
import edu.wpi.first.networktables.DoublePublisher;
import edu.wpi.first.networktables.IntegerPublisher;
import edu.wpi.first.networktables.IntegerSubscriber;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.networktables.NetworkTablesJNI;
import edu.wpi.first.networktables.PubSubOption;
import edu.wpi.first.networktables.RawPublisher;
import edu.wpi.first.networktables.StringPublisher;
import org.lumenvision.photoncompat.LumenCamera;
import org.lumenvision.photoncompat.compat.PhotonCamera;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Random;

/**
 * A simulated LumenVision camera: given its pose in the field and the targets around it, it projects them through a pinhole model and
 * publishes the same NetworkTables topics the coprocessor would, so {@link LumenCamera} / {@code PhotonCamera} and the pose estimators
 * read it unchanged. Usually driven by a {@link VisionSystemSim}; {@link #update(long, Pose3d, Collection)} can be called directly.
 *
 * <p>Targets are detected when they face the camera and all their corners are inside the image with enough pixel area. Poses are
 * ground truth (zero ambiguity); the properties' pixel noise moves the reported corners only. Targets do not occlude each other.
 * Multi-tag and constrained results are published when their inputs are there, exactly as the coprocessor does.
 */
public class PhotonCameraSim implements AutoCloseable {
    private final LumenCamera camera;
    private final SimCameraProperties properties;
    private final Random random = new Random();

    private final RawPublisher resultPub;
    private final DoubleArrayPublisher intrinsicsPub;
    private final DoubleArrayPublisher distortionPub;
    private final DoublePublisher heartbeatPub;
    private final StringPublisher versionPub;
    private final IntegerPublisher pipelineStatusPub;
    private final BooleanPublisher driverStatusPub;
    private final IntegerPublisher fpsStatusPub;
    private final IntegerPublisher ledStatusPub;
    private final IntegerSubscriber pipelineConfigSub;
    private final BooleanSubscriber driverConfigSub;
    private final IntegerSubscriber fpsConfigSub;
    private final IntegerSubscriber ledConfigSub;
    private final DoubleArraySubscriber seedSub;
    private final DoubleArraySubscriber mountSub;

    private Pose3d layoutOrigin = new Pose3d();
    private double maxSightRangeMeters = Double.POSITIVE_INFINITY;
    private boolean enabled = true;
    private long sequence = 0;
    private long heartbeat = 0;
    private long nextFrameMicros = 0;
    private long lastPublishedMicros = -1;

    public PhotonCameraSim(LumenCamera camera, SimCameraProperties properties) {
        this.camera = camera;
        this.properties = properties.copy();

        NetworkTableInstance instance = camera.getNetworkTableInstance();
        String root = "/" + camera.getRootTable() + "/";
        String base = root + LumenCamera.tableName(camera.getName()) + "/";
        resultPub = instance.getRawTopic(base + "result").publish("raw", PubSubOption.sendAll(true), PubSubOption.keepDuplicates(true));
        intrinsicsPub = instance.getDoubleArrayTopic(base + "cameraIntrinsics").publish();
        distortionPub = instance.getDoubleArrayTopic(base + "cameraDistortion").publish();
        heartbeatPub = instance.getDoubleTopic(root + "heartbeat").publish();
        versionPub = instance.getStringTopic(root + ".version").publish();
        pipelineStatusPub = instance.getIntegerTopic(base + "status/pipelineIndex").publish();
        driverStatusPub = instance.getBooleanTopic(base + "status/driverMode").publish();
        fpsStatusPub = instance.getIntegerTopic(base + "status/fpsLimit").publish();
        ledStatusPub = instance.getIntegerTopic(root + "status/ledMode").publish();
        pipelineConfigSub = instance.getIntegerTopic(base + "config/pipelineIndex").subscribe(-1);
        driverConfigSub = instance.getBooleanTopic(base + "config/driverMode").subscribe(false);
        fpsConfigSub = instance.getIntegerTopic(base + "config/fpsLimit").subscribe(-1);
        ledConfigSub = instance.getIntegerTopic(root + "config/ledMode").subscribe(-1);
        seedSub = instance.getDoubleArrayTopic(base + "config/constrainedSeed").subscribe(new double[0]);
        mountSub = instance.getDoubleArrayTopic(base + "config/robotToCamera").subscribe(new double[0]);
    }

    public PhotonCameraSim(PhotonCamera camera, SimCameraProperties properties) {
        this(camera.getLumenCamera(), properties);
    }

    public LumenCamera getCamera() {
        return camera;
    }

    public SimCameraProperties getProperties() {
        return properties;
    }

    /** Seeds the noise and latency jitter so a run is repeatable. */
    public void setRandomSeed(long seed) {
        random.setSeed(seed);
    }

    /** Targets farther than this are not detected (default unlimited). */
    public void setMaxSightRange(double meters) {
        this.maxSightRangeMeters = meters;
    }

    /** A disabled camera publishes nothing, as if unplugged. */
    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    /** The frame of the field layout the coprocessor would solve in; {@link VisionSystemSim#addAprilTags} sets this. */
    public void setLayoutOrigin(Pose3d layoutOrigin) {
        this.layoutOrigin = layoutOrigin;
    }

    /** Runs the camera at the current time; see {@link #update(long, Pose3d, Collection)}. */
    public boolean update(Pose3d cameraPoseInField, Collection<VisionTargetSim> targets) {
        return update(NetworkTablesJNI.now(), cameraPoseInField, targets);
    }

    /**
     * Captures a frame if the camera's frame interval has elapsed and publishes its result, stamped with the capture time
     * ({@code nowMicros} minus a latency sample).
     *
     * @param nowMicros the NT clock, in microseconds
     * @param cameraPoseInField the camera's pose in the same frame as the targets
     * @return whether a result was published
     */
    public boolean update(long nowMicros, Pose3d cameraPoseInField, Collection<VisionTargetSim> targets) {
        if (!enabled) return false;
        publishStatus();
        if (nowMicros < nextFrameMicros) return false;

        double interval = properties.getFrameIntervalMs();
        long fpsLimit = fpsConfigSub.get();
        if (fpsLimit > 0) interval = Math.max(interval, 1000.0 / fpsLimit);
        nextFrameMicros = nowMicros + Math.round(interval * 1000.0);

        boolean driverMode = driverConfigSub.get();
        List<SimResultEncoder.Target> detected = driverMode ? new ArrayList<>() : detect(cameraPoseInField, targets);

        long latencyMicros = Math.round(Math.max(0.0, properties.getAvgLatencyMs() + random.nextGaussian() * properties.getLatencyStdDevMs()) * 1000.0);
        long captureMicros = Math.max(1, nowMicros - latencyMicros);
        if (captureMicros <= lastPublishedMicros) captureMicros = lastPublishedMicros + 1; // NT topics keep timestamps non-decreasing
        lastPublishedMicros = captureMicros;

        List<Integer> multiTagIds = new ArrayList<>();
        SimResultEncoder.MultiTag multiTag = null;
        long fiducialCount = detected.stream().filter(t -> t.fiducialId() >= 0).count();
        if (fiducialCount >= 2) {
            for (SimResultEncoder.Target target : detected) if (target.fiducialId() >= 0) multiTagIds.add(target.fiducialId());
            multiTag = new SimResultEncoder.MultiTag(layoutOrigin.plus(new Transform3d(new Pose3d(), cameraPoseInField)), 0.0);
        }

        SimResultEncoder.Constrained constrained = null;
        double[] seed = seedSub.get();
        double[] mount = mountSub.get();
        if (fiducialCount >= 1 && seed.length == 3 && mount.length == 7) {
            Transform3d robotToCamera = new Transform3d(new Translation3d(mount[0], mount[1], mount[2]),
                    new Rotation3d(new Quaternion(mount[3], mount[4], mount[5], mount[6])));
            Pose3d robotInLayout = layoutOrigin.plus(new Transform3d(new Pose3d(), cameraPoseInField.transformBy(robotToCamera.inverse())));
            Pose2d floor = robotInLayout.toPose2d();
            constrained = new SimResultEncoder.Constrained(floor.getX(), floor.getY(), floor.getRotation().getRadians(), 0.0, (int) fiducialCount);
        }

        byte[] packet = SimResultEncoder.encode(++sequence, latencyMicros, multiTag, multiTagIds, constrained, detected);
        resultPub.set(packet, captureMicros);
        return true;
    }

    private void publishStatus() {
        heartbeatPub.set(heartbeat++);
        versionPub.set(LumenCamera.getLibraryVersion());
        intrinsicsPub.set(properties.getIntrinsics());
        distortionPub.set(properties.getDistCoeffs());
        pipelineStatusPub.set(Math.max(0, pipelineConfigSub.get()));
        driverStatusPub.set(driverConfigSub.get());
        fpsStatusPub.set(fpsConfigSub.get());
        ledStatusPub.set(ledConfigSub.get());
    }

    private List<SimResultEncoder.Target> detect(Pose3d cameraPose, Collection<VisionTargetSim> targets) {
        List<SimResultEncoder.Target> detected = new ArrayList<>();
        for (VisionTargetSim target : targets) {
            SimResultEncoder.Target result = project(cameraPose, target);
            if (result != null) detected.add(result);
        }
        return detected;
    }

    // camera-frame (X forward, Y left, Z up) position of a field point
    private static Translation3d toCameraFrame(Pose3d cameraPose, Translation3d fieldPoint) {
        return fieldPoint.minus(cameraPose.getTranslation()).rotateBy(cameraPose.getRotation().unaryMinus());
    }

    private double[] pixel(Translation3d inCamera) {
        return new double[] {
                properties.getCx() - properties.getFx() * (inCamera.getY() / inCamera.getX()),
                properties.getCy() - properties.getFy() * (inCamera.getZ() / inCamera.getX()) };
    }

    private SimResultEncoder.Target project(Pose3d cameraPose, VisionTargetSim target) {
        Pose3d targetPose = target.getPose();
        TargetModel model = target.getModel();
        Translation3d cameraPosition = cameraPose.getTranslation();
        if (cameraPosition.getDistance(targetPose.getTranslation()) > maxSightRangeMeters) return null;

        if (model.isPlanar()) {
            // a flat target is only seen from its front: the face normal (X) must point towards the camera
            Translation3d normal = new Translation3d(1, 0, 0).rotateBy(targetPose.getRotation());
            Translation3d toCamera = cameraPosition.minus(targetPose.getTranslation());
            if (normal.getX() * toCamera.getX() + normal.getY() * toCamera.getY() + normal.getZ() * toCamera.getZ() <= 0) return null;
        }

        List<Translation3d> fieldVertices = model.getFieldVertices(targetPose);
        double[][] pixels = new double[fieldVertices.size()][];
        for (int i = 0; i < pixels.length; i++) {
            Translation3d inCamera = toCameraFrame(cameraPose, fieldVertices.get(i));
            if (inCamera.getX() <= 1e-6) return null; // behind or at the lens
            pixels[i] = pixel(inCamera);
        }

        double[] corners;
        if (model.isPlanar()) {
            corners = new double[8];
            for (int i = 0; i < 4; i++) {
                corners[i * 2] = pixels[i][0];
                corners[i * 2 + 1] = pixels[i][1];
            }
        } else {
            double minX = Double.MAX_VALUE, minY = Double.MAX_VALUE, maxX = -Double.MAX_VALUE, maxY = -Double.MAX_VALUE;
            for (double[] p : pixels) {
                minX = Math.min(minX, p[0]);
                maxX = Math.max(maxX, p[0]);
                minY = Math.min(minY, p[1]);
                maxY = Math.max(maxY, p[1]);
            }
            corners = new double[] { minX, maxY, maxX, maxY, maxX, minY, minX, minY };
        }

        // the coprocessor needs every corner in the image to decode a tag
        for (int i = 0; i < 4; i++) {
            if (corners[i * 2] < 0 || corners[i * 2] > properties.getResWidth() || corners[i * 2 + 1] < 0 || corners[i * 2 + 1] > properties.getResHeight()) return null;
        }

        double areaPixels = polygonArea(corners);
        if (areaPixels < properties.getMinTargetAreaPixels()) return null;

        // the corner noise is applied after visibility, as sensor noise would be
        double[] noisy = corners.clone();
        for (int i = 0; i < 4; i++) {
            double magnitude = Math.max(0.0, properties.getAvgErrorPixels() + random.nextGaussian() * properties.getErrorStdDevPixels());
            double direction = random.nextDouble() * 2.0 * Math.PI;
            noisy[i * 2] += magnitude * Math.cos(direction);
            noisy[i * 2 + 1] += magnitude * Math.sin(direction);
        }

        double centreX = 0, centreY = 0;
        for (int i = 0; i < 4; i++) {
            centreX += noisy[i * 2] / 4.0;
            centreY += noisy[i * 2 + 1] / 4.0;
        }
        double yaw = Math.toDegrees(Math.atan((properties.getCx() - centreX) / properties.getFx()));
        double pitch = Math.toDegrees(Math.atan((properties.getCy() - centreY) / properties.getFy()));
        double areaPercent = 100.0 * areaPixels / ((double) properties.getResWidth() * properties.getResHeight());

        double[] rect = new double[8];
        double skew = minAreaRect(noisy, rect);

        return new SimResultEncoder.Target(target.getFiducialId(), yaw, pitch, areaPercent, skew,
                new Transform3d(cameraPose, targetPose), noisy, rect);
    }

    private static double polygonArea(double[] corners) {
        double sum = 0;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            sum += corners[i * 2] * corners[j * 2 + 1] - corners[j * 2] * corners[i * 2 + 1];
        }
        return Math.abs(sum) / 2.0;
    }

    /**
     * The smallest rectangle around four points, found by trying each edge direction. Writes its corners (x0,y0..x3,y3) to
     * {@code rectOut} and returns the rotation of its first edge in degrees, in [0, 90).
     */
    static double minAreaRect(double[] corners, double[] rectOut) {
        double bestArea = Double.MAX_VALUE;
        double bestAngle = 0;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            double angle = Math.atan2(corners[j * 2 + 1] - corners[i * 2 + 1], corners[j * 2] - corners[i * 2]);
            double c = Math.cos(angle), s = Math.sin(angle);
            double minU = Double.MAX_VALUE, maxU = -Double.MAX_VALUE, minV = Double.MAX_VALUE, maxV = -Double.MAX_VALUE;
            for (int k = 0; k < 4; k++) {
                double u = corners[k * 2] * c + corners[k * 2 + 1] * s;
                double v = -corners[k * 2] * s + corners[k * 2 + 1] * c;
                minU = Math.min(minU, u);
                maxU = Math.max(maxU, u);
                minV = Math.min(minV, v);
                maxV = Math.max(maxV, v);
            }
            double area = (maxU - minU) * (maxV - minV);
            if (area < bestArea) {
                bestArea = area;
                bestAngle = angle;
                double[][] box = { { minU, minV }, { maxU, minV }, { maxU, maxV }, { minU, maxV } };
                for (int k = 0; k < 4; k++) {
                    rectOut[k * 2] = box[k][0] * c - box[k][1] * s;
                    rectOut[k * 2 + 1] = box[k][0] * s + box[k][1] * c;
                }
            }
        }
        double degrees = Math.toDegrees(bestAngle) % 90.0;
        return degrees < 0 ? degrees + 90.0 : degrees;
    }

    @Override
    public void close() {
        for (AutoCloseable closeable : new AutoCloseable[] {
                resultPub, intrinsicsPub, distortionPub, heartbeatPub, versionPub, pipelineStatusPub, driverStatusPub, fpsStatusPub,
                ledStatusPub, pipelineConfigSub, driverConfigSub, fpsConfigSub, ledConfigSub, seedSub, mountSub }) {
            try {
                closeable.close();
            } catch (Exception ignored) {
                // closing a topic handle cannot fail in a way the caller could act on
            }
        }
    }
}
