package org.lumenvision.photoncompat.compat;

import org.lumenvision.photoncompat.LumenLedMode;

/** The vision LED mode, named and numbered as photonlib's VisionLEDMode. */
public enum VisionLEDMode {
    kDefault(-1),
    kOff(0),
    kOn(1),
    kBlink(2);

    public final int value;

    VisionLEDMode(int value) {
        this.value = value;
    }

    LumenLedMode toLumen() {
        return LumenLedMode.fromValue(value);
    }

    static VisionLEDMode fromLumen(LumenLedMode mode) {
        for (VisionLEDMode candidate : values()) {
            if (candidate.value == mode.value) return candidate;
        }
        return kDefault;
    }
}
