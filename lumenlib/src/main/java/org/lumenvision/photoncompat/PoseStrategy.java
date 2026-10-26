package org.lumenvision.photoncompat;

/** How {@link PhotonPoseEstimator} estimates the robot pose; named as photonlib's PoseStrategy. */
public enum PoseStrategy {
    LOWEST_AMBIGUITY,
    CLOSEST_TO_CAMERA_HEIGHT,
    CLOSEST_TO_REFERENCE_POSE,
    CLOSEST_TO_LAST_POSE,
    AVERAGE_BEST_TARGETS,
    MULTI_TAG_PNP_ON_COPROCESSOR,
    /** LumenVision has no roboRIO-side PnP; this runs the coprocessor's multi-tag solve instead. */
    MULTI_TAG_PNP_ON_RIO,
    PNP_DISTANCE_TRIG_SOLVE,
    /** The coprocessor's solve constrained to a robot flat on the floor, seeded by a reference pose. */
    CONSTRAINED_SOLVEPNP
}
