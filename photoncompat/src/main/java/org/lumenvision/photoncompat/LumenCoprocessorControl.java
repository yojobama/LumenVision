package org.lumenvision.photoncompat;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

/**
 * REST-based control of a LumenVision coprocessor: driver mode, pipeline profiles and snapshots.
 * Each call is a blocking HTTP request, so call it from event-driven code, not a periodic loop. For
 * match recording use {@link LumenCoprocessor}.
 *
 * @param baseUrl the coprocessor's web address, e.g. {@code http://lumenvision.local:5800}, with no
 *     trailing slash or {@code /api} suffix
 */
public class LumenCoprocessorControl {
    private final HttpClient client = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(2)).build();
    private final String baseUrl;

    public LumenCoprocessorControl(String baseUrl) {
        this.baseUrl = baseUrl;
    }

    // every REST route lives under /api
    private URI api(String path) {
        return URI.create(baseUrl + "/api" + path);
    }

    /**
     * Toggles driver mode on a detection sink: it keeps streaming video but stops detection and
     * publishing results.
     *
     * @param sinkId the coprocessor-side sink id (not the source id)
     * @return true if the request succeeded; false on any network/HTTP failure
     */
    public boolean setDriverMode(int sinkId, boolean enabled) {
        return sendPatch("/sink/driverMode?SinkID=" + sinkId + "&Enabled=" + enabled);
    }

    /** Whether driver mode is on for the given sink; false on failure (indistinguishable from off). */
    public boolean getDriverMode(int sinkId) {
        try {
            HttpRequest request =
                    HttpRequest.newBuilder(api("/sink/driverMode?SinkID=" + sinkId))
                            .GET()
                            .timeout(Duration.ofSeconds(2))
                            .build();
            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
            return response.statusCode() == 200 && Boolean.parseBoolean(response.body().trim());
        } catch (IOException | InterruptedException e) {
            if (e instanceof InterruptedException) Thread.currentThread().interrupt();
            return false;
        }
    }

    /**
     * Switches the pipeline profile running for a camera source (PhotonVision's {@code
     * setPipelineIndex}); the detection sink is rebuilt from the profile while bound previews and
     * NT4 publishing keep working.
     *
     * @param sourceId the coprocessor-side camera source id (not a sink id)
     * @param profileIndex a profile index returned when the profile was created
     * @return true if the request succeeded; false on any network/HTTP failure
     */
    public boolean setPipelineIndex(int sourceId, int profileIndex) {
        return sendPatch("/source/profiles/activate?sourceId=" + sourceId + "&index=" + profileIndex);
    }

    /**
     * The active pipeline profile index for a camera source, or -1 if none is active or on any
     * network/HTTP failure.
     */
    public int getPipelineIndex(int sourceId) {
        try {
            HttpRequest request =
                    HttpRequest.newBuilder(api("/source/profiles/active?sourceId=" + sourceId))
                            .GET()
                            .timeout(Duration.ofSeconds(2))
                            .build();
            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
            return response.statusCode() == 200 ? Integer.parseInt(response.body().trim()) : -1;
        } catch (IOException | InterruptedException | NumberFormatException e) {
            if (e instanceof InterruptedException) Thread.currentThread().interrupt();
            return -1;
        }
    }

    /**
     * Saves a source's latest frame to a file on the coprocessor (not transferred to the robot).
     *
     * @param sourceId the coprocessor-side source id
     * @param fileName a bare file name, e.g. {@code "match17-auto.png"}; it is resolved under the
     *     coprocessor's snapshots directory
     * @return true if the coprocessor reports it saved successfully
     */
    public boolean saveSnapshot(int sourceId, String fileName) {
        return sendPost("/source/snapshot?SourceID=" + sourceId + "&fileName=" + fileName);
    }

    private boolean sendPatch(String path) {
        return sendNoBody(path, "PATCH");
    }

    private boolean sendPost(String path) {
        return sendNoBody(path, "POST");
    }

    private boolean sendNoBody(String path, String method) {
        try {
            HttpRequest request =
                    HttpRequest.newBuilder(api(path))
                            .method(method, HttpRequest.BodyPublishers.noBody())
                            .timeout(Duration.ofSeconds(2))
                            .build();
            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
            return response.statusCode() >= 200 && response.statusCode() < 300;
        } catch (IOException | InterruptedException e) {
            if (e instanceof InterruptedException) Thread.currentThread().interrupt();
            return false;
        }
    }
}
