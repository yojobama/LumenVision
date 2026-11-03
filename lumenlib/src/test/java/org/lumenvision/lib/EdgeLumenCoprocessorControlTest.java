package org.lumenvision.lib;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;

/** Snapshot file names that are ordinary on a robot (event names have spaces) but are not valid raw in a URL query. */
@Tag("edge")
class EdgeLumenCoprocessorControlTest {
    private HttpServer server;
    private final List<String> fileNames = new ArrayList<>();
    private volatile boolean reply = true;

    @BeforeEach
    void start() throws IOException {
        server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext("/api/source/snapshot", exchange -> {
            String query = exchange.getRequestURI().getRawQuery();
            for (String pair : query == null ? new String[0] : query.split("&")) {
                if (pair.startsWith("fileName=")) {
                    synchronized (fileNames) {
                        fileNames.add(URLDecoder.decode(pair.substring("fileName=".length()), StandardCharsets.UTF_8));
                    }
                }
            }
            // SourceController.SaveSnapshot returns Task<bool>: ASP.NET answers 200 with a JSON body "true" or "false"
            byte[] body = (reply ? "true" : "false").getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().add("Content-Type", "application/json; charset=utf-8");
            exchange.sendResponseHeaders(200, body.length);
            exchange.getResponseBody().write(body);
            exchange.close();
        });
        server.start();
    }

    @AfterEach
    void stop() {
        server.stop(0);
    }

    private LumenCoprocessorControl control() {
        return new LumenCoprocessorControl("http://127.0.0.1:" + server.getAddress().getPort());
    }

    // e.g. saveSnapshot(0, DriverStation.getEventName() + " Q" + match + ".png") at an event called "Week 0"
    @Test
    void G3_aFileNameWithASpaceIsSavedRatherThanThrowing() {
        boolean[] saved = new boolean[1];
        assertDoesNotThrow(() -> saved[0] = control().saveSnapshot(3, "Week 0 Q12.png"),
                "saveSnapshot must not throw out of robot code for a file name with a space");
        assertTrue(saved[0]);
        assertEquals(List.of("Week 0 Q12.png"), fileNames);
    }

    @Test
    void G3_aFileNameWithAHashOrAmpersandReachesTheCoprocessorWhole() {
        assertTrue(control().saveSnapshot(3, "auto#2.png"));
        assertTrue(control().saveSnapshot(3, "red&blue.png"));
        assertEquals(List.of("auto#2.png", "red&blue.png"), fileNames);
    }

    // ManagerWrapper.SaveSnapshot is false when the source has no frame yet (camera unplugged, node stopped) or the write failed
    @Test
    void G4_aSnapshotTheCoprocessorReportsUnsavedIsNotReportedSaved() {
        reply = false;
        assertFalse(control().saveSnapshot(3, "auto.png"), "the coprocessor answered 200 with body false (nothing saved)");
    }
}
