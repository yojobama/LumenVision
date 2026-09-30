package org.lumenvision.photoncompat;

/** The coprocessor's vision-LED mode, numbered as photonlib's VisionLEDMode. */
public enum LumenLedMode {
    /** The coprocessor's default: off. */
    DEFAULT(-1),
    OFF(0),
    ON(1),
    BLINK(2);

    public final int value;

    LumenLedMode(int value) {
        this.value = value;
    }

    /** @return the mode for a value written to/read from NetworkTables, {@link #DEFAULT} if unknown */
    public static LumenLedMode fromValue(int value) {
        for (LumenLedMode mode : values()) {
            if (mode.value == value) return mode;
        }
        return DEFAULT;
    }
}
