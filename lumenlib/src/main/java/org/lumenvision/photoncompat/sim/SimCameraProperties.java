package org.lumenvision.photoncompat.sim;

import edu.wpi.first.math.Matrix;
import edu.wpi.first.math.geometry.Rotation2d;
import edu.wpi.first.math.numbers.N1;
import edu.wpi.first.math.numbers.N3;
import edu.wpi.first.math.numbers.N8;

/**
 * What a simulated camera looks like: pinhole intrinsics (no lens distortion is applied when projecting), how noisy its corner
 * pixels are, how fast it runs and how long a frame takes to reach the robot. The presets approximate real cameras from their field
 * of view; calibrate with {@link #setCalibration(int, int, Matrix, Matrix)} to match a specific camera.
 */
public class SimCameraProperties {
    private int width = 960;
    private int height = 720;
    private double fx;
    private double fy;
    private double cx;
    private double cy;
    private double[] distCoeffs = new double[8];
    private double avgErrorPixels = 0.0;
    private double errorStdDevPixels = 0.0;
    private double frameIntervalMs = 1000.0 / 30.0;
    private double avgLatencyMs = 0.0;
    private double latencyStdDevMs = 0.0;
    private double minTargetAreaPixels = 100.0;

    /** A 960x720 camera with a 90 degree diagonal field of view, no noise and no latency. */
    public SimCameraProperties() {
        setCalibration(960, 720, Rotation2d.fromDegrees(90));
    }

    public SimCameraProperties copy() {
        SimCameraProperties copy = new SimCameraProperties();
        copy.width = width;
        copy.height = height;
        copy.fx = fx;
        copy.fy = fy;
        copy.cx = cx;
        copy.cy = cy;
        copy.distCoeffs = distCoeffs.clone();
        copy.avgErrorPixels = avgErrorPixels;
        copy.errorStdDevPixels = errorStdDevPixels;
        copy.frameIntervalMs = frameIntervalMs;
        copy.avgLatencyMs = avgLatencyMs;
        copy.latencyStdDevMs = latencyStdDevMs;
        copy.minTargetAreaPixels = minTargetAreaPixels;
        return copy;
    }

    /** Ideal pinhole intrinsics for an image of this size and this diagonal field of view. */
    public SimCameraProperties setCalibration(int width, int height, Rotation2d fovDiag) {
        if (fovDiag.getDegrees() < 1.0 || fovDiag.getDegrees() > 179.0) throw new IllegalArgumentException("the diagonal field of view must be 1 to 179 degrees");
        double focalPixels = (Math.hypot(width, height) / 2.0) / Math.tan(fovDiag.getRadians() / 2.0);
        this.width = width;
        this.height = height;
        this.fx = focalPixels;
        this.fy = focalPixels;
        this.cx = width / 2.0;
        this.cy = height / 2.0;
        this.distCoeffs = new double[8];
        return this;
    }

    /** Measured intrinsics: a 3x3 camera matrix and the 8 distortion coefficients (kept for {@code cameraDistortion}, not used to project). */
    public SimCameraProperties setCalibration(int width, int height, Matrix<N3, N3> cameraMatrix, Matrix<N8, N1> distCoeffs) {
        this.width = width;
        this.height = height;
        this.fx = cameraMatrix.get(0, 0);
        this.fy = cameraMatrix.get(1, 1);
        this.cx = cameraMatrix.get(0, 2);
        this.cy = cameraMatrix.get(1, 2);
        this.distCoeffs = distCoeffs.getData().clone();
        return this;
    }

    /** Corner pixel noise: each corner moves a random direction by {@code avg + N(0, stdDev)} pixels (never negative). */
    public SimCameraProperties setCalibError(double avgErrorPixels, double errorStdDevPixels) {
        this.avgErrorPixels = avgErrorPixels;
        this.errorStdDevPixels = errorStdDevPixels;
        return this;
    }

    public SimCameraProperties setFPS(double fps) {
        if (fps <= 0) throw new IllegalArgumentException("fps must be positive");
        this.frameIntervalMs = 1000.0 / fps;
        return this;
    }

    public SimCameraProperties setAvgLatencyMs(double avgLatencyMs) {
        this.avgLatencyMs = avgLatencyMs;
        return this;
    }

    public SimCameraProperties setLatencyStdDevMs(double latencyStdDevMs) {
        this.latencyStdDevMs = latencyStdDevMs;
        return this;
    }

    /** Targets smaller than this many pixels (of projected area) are not detected. */
    public SimCameraProperties setMinTargetAreaPixels(double pixels) {
        this.minTargetAreaPixels = pixels;
        return this;
    }

    public int getResWidth() {
        return width;
    }

    public int getResHeight() {
        return height;
    }

    public double getFx() {
        return fx;
    }

    public double getFy() {
        return fy;
    }

    public double getCx() {
        return cx;
    }

    public double getCy() {
        return cy;
    }

    /** The row-major 3x3 intrinsics matrix as published in {@code cameraIntrinsics}. */
    public double[] getIntrinsics() {
        return new double[] { fx, 0, cx, 0, fy, cy, 0, 0, 1 };
    }

    public double[] getDistCoeffs() {
        return distCoeffs.clone();
    }

    public double getAvgErrorPixels() {
        return avgErrorPixels;
    }

    public double getErrorStdDevPixels() {
        return errorStdDevPixels;
    }

    public double getFrameIntervalMs() {
        return frameIntervalMs;
    }

    public double getFPS() {
        return 1000.0 / frameIntervalMs;
    }

    public double getAvgLatencyMs() {
        return avgLatencyMs;
    }

    public double getLatencyStdDevMs() {
        return latencyStdDevMs;
    }

    public double getMinTargetAreaPixels() {
        return minTargetAreaPixels;
    }

    /** The horizontal field of view. */
    public Rotation2d getHorizFOV() {
        return new Rotation2d(2.0 * Math.atan(width / (2.0 * fx)));
    }

    /** The vertical field of view. */
    public Rotation2d getVertFOV() {
        return new Rotation2d(2.0 * Math.atan(height / (2.0 * fy)));
    }

    // ---- presets (approximated from each camera's field of view; no lens distortion) ----

    public static SimCameraProperties PERFECT_90DEG() {
        return new SimCameraProperties();
    }

    /** Microsoft LifeCam HD-3000 (about 68.5 degrees diagonal) at 320x240, 15 fps. */
    public static SimCameraProperties LIFECAM_320_240() {
        return new SimCameraProperties().setCalibration(320, 240, Rotation2d.fromDegrees(68.5)).setCalibError(0.35, 0.10).setFPS(15)
                .setAvgLatencyMs(35).setLatencyStdDevMs(5);
    }

    /** Microsoft LifeCam HD-3000 at 640x480, 15 fps. */
    public static SimCameraProperties LIFECAM_640_480() {
        return new SimCameraProperties().setCalibration(640, 480, Rotation2d.fromDegrees(68.5)).setCalibError(0.25, 0.08).setFPS(15)
                .setAvgLatencyMs(35).setLatencyStdDevMs(8);
    }

    /** Limelight 2 (about 75 degrees diagonal) at 640x480, 30 fps. */
    public static SimCameraProperties LL2_640_480() {
        return new SimCameraProperties().setCalibration(640, 480, Rotation2d.fromDegrees(75.7)).setCalibError(0.25, 0.08).setFPS(30)
                .setAvgLatencyMs(30).setLatencyStdDevMs(6);
    }

    /** Limelight 2 at 960x720, 18 fps. */
    public static SimCameraProperties LL2_960_720() {
        return new SimCameraProperties().setCalibration(960, 720, Rotation2d.fromDegrees(75.7)).setCalibError(0.35, 0.10).setFPS(18)
                .setAvgLatencyMs(45).setLatencyStdDevMs(8);
    }

    /** OV9281 global-shutter camera with a typical 80 degree lens at 1280x800, 60 fps. */
    public static SimCameraProperties OV9281_1280_800() {
        return new SimCameraProperties().setCalibration(1280, 800, Rotation2d.fromDegrees(80)).setCalibError(0.25, 0.08).setFPS(60)
                .setAvgLatencyMs(20).setLatencyStdDevMs(4);
    }
}
