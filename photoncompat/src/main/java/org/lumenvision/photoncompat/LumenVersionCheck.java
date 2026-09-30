package org.lumenvision.photoncompat;

import edu.wpi.first.wpilibj.DriverStation;

import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;

/**
 * Compares this jar's version with the coprocessor's {@code .version} NT4 topic (NetworkTablesSink)
 * and warns on the driver station when they differ.
 */
final class LumenVersionCheck {
    private static final String JAR_VERSION = loadJarVersion();

    private LumenVersionCheck() {}

    static String jarVersion() {
        return JAR_VERSION;
    }

    private static String loadJarVersion() {
        // built from the template under src/main/resources by processResources expand()
        try (InputStream in = LumenVersionCheck.class.getResourceAsStream("/photoncompat-version.properties")) {
            if (in == null) return "unknown";
            Properties props = new Properties();
            props.load(in);
            return props.getProperty("version", "unknown");
        } catch (IOException e) {
            return "unknown";
        }
    }

    /**
     * @param coprocessorVersion the coprocessor's {@code .version} topic value; an empty string
     *     (NT4 default, nothing received yet) is ignored
     */
    static void warnOnMismatch(String coprocessorVersion) {
        if (coprocessorVersion == null || coprocessorVersion.isEmpty()) return;
        if (coprocessorVersion.equals(JAR_VERSION)) return;
        report("LumenVision: photoncompat-java version (" + JAR_VERSION
                + ") does not match the coprocessor's own reported version ("
                + coprocessorVersion + ") - update one to match the other.");
    }

    /** A driver-station warning; falls back to stderr where the HAL is unavailable (unit tests, desktop tools). */
    static void report(String message) {
        try {
            DriverStation.reportWarning(message, false);
        } catch (Throwable t) {
            System.err.println(message);
        }
    }
}
