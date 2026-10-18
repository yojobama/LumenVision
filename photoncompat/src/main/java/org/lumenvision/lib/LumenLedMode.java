package org.lumenvision.lib;

/** The coprocessor's vision-LED mode: {@link #AUTO} lights it while any detector runs, the others are fixed. */
public enum LumenLedMode {
    AUTO(-1),
    OFF(0),
    ON(1),
    BLINK(2);

    public final int value;

    LumenLedMode(int value) {
        this.value = value;
    }

    /** @return the mode for a value written to/read from NetworkTables, {@link #AUTO} if unknown */
    public static LumenLedMode fromValue(int value) {
        for (LumenLedMode mode : values()) {
            if (mode.value == value) return mode;
        }
        return AUTO;
    }
}
