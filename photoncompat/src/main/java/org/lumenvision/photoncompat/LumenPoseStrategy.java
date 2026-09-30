package org.lumenvision.photoncompat;

/** How {@link LumenPoseEstimator} turns a result into a robot pose; named as photonlib's PoseStrategy. */
public enum LumenPoseStrategy {
    /** The pose from the single target with the lowest pose ambiguity. */
    LOWEST_AMBIGUITY,
    /** The pose whose camera height is closest to the camera's mount height (the robot is on the floor). */
    CLOSEST_TO_CAMERA_HEIGHT,
    /** The pose closest to a reference pose supplied with {@link LumenPoseEstimator#setReferencePose}. */
    CLOSEST_TO_REFERENCE_POSE,
    /** The pose closest to the previous estimate. */
    CLOSEST_TO_LAST_POSE,
    /** An ambiguity-weighted average of the poses from every visible target. */
    AVERAGE_BEST_TARGETS,
    /** The coprocessor's joint solve over every visible tag (the most accurate; needs a field layout uploaded to the coprocessor). */
    MULTI_TAG_PNP_ON_COPROCESSOR,
    /** Distance and bearing to the best tag plus the robot's heading ({@link LumenPoseEstimator#addHeadingData}). */
    PNP_DISTANCE_TRIG_SOLVE,
    /** The coprocessor's solve constrained to a robot flat on the floor, seeded by a reference pose. */
    CONSTRAINED_SOLVEPNP
}
