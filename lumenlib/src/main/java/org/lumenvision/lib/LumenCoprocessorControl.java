package org.lumenvision.lib;

import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

/**
 * REST-based control of a LumenVision coprocessor: saving snapshots to a file on it.
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
     * Saves a source's latest frame to a file on the coprocessor (not transferred to the robot).
     *
     * @param sourceId the coprocessor-side source id
     * @param fileName a bare file name, e.g. {@code "match17-auto.png"}; it is resolved under the
     *     coprocessor's snapshots directory
     * @return true if the coprocessor reports it saved successfully
     */
    public boolean saveSnapshot(int sourceId, String fileName) {
        // encoded: a space, '#' or '&' in an event-named file would otherwise throw or cut the name short
        String body = send("/source/snapshot?SourceID=" + sourceId + "&fileName=" + URLEncoder.encode(fileName, StandardCharsets.UTF_8), "POST");
        // the coprocessor answers 200 with "false" when it had no frame to save or the write failed
        return body != null && Boolean.parseBoolean(body.trim());
    }

    private boolean sendPatch(String path) {
        return sendNoBody(path, "PATCH");
    }

    private boolean sendPost(String path) {
        return sendNoBody(path, "POST");
    }

    private boolean sendNoBody(String path, String method) {
        return send(path, method) != null;
    }

    // the response body of a 2xx answer, or null when the request failed or was refused
    private String send(String path, String method) {
        try {
            HttpRequest request =
                    HttpRequest.newBuilder(api(path))
                            .method(method, HttpRequest.BodyPublishers.noBody())
                            .timeout(Duration.ofSeconds(2))
                            .build();
            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
            return response.statusCode() >= 200 && response.statusCode() < 300 ? response.body() : null;
        } catch (IOException | InterruptedException | IllegalArgumentException e) {
            if (e instanceof InterruptedException) Thread.currentThread().interrupt();
            return null;
        }
    }
}
