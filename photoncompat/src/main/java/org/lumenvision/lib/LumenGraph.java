package org.lumenvision.lib;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Optional;

/** The coprocessor's node graph as published under {@code <rootTable>/.graph}: every camera, crop and sink with its state and input. */
public final class LumenGraph {
    private static final ObjectMapper MAPPER = new ObjectMapper();

    /** One node: {@code input} is the id of the node it reads from, or -1 for none. */
    public static final class Node {
        private final int id;
        private final String name;
        private final String kind;
        private final boolean enabled;
        private final int input;

        Node(int id, String name, String kind, boolean enabled, int input) {
            this.id = id;
            this.name = name;
            this.kind = kind;
            this.enabled = enabled;
            this.input = input;
        }

        public int getId() {
            return id;
        }

        /** The name its topics are published under (a detector made for a camera carries the camera's name). */
        public String getName() {
            return name;
        }

        /** The coprocessor's type for the node, e.g. {@code Camera}, {@code Crop}, {@code ApriltagSink}, {@code ObjectDetectionSink}. */
        public String getKind() {
            return kind;
        }

        public boolean isEnabled() {
            return enabled;
        }

        public int getInput() {
            return input;
        }

        public boolean isDetector() {
            return kind.equals("ApriltagSink") || kind.equals("ObjectDetectionSink");
        }

        public boolean isCamera() {
            return kind.equals("Camera") || kind.equals("Crop");
        }
    }

    private final List<Node> nodes;

    private LumenGraph(List<Node> nodes) {
        this.nodes = Collections.unmodifiableList(nodes);
    }

    /** An empty graph, for a coprocessor that has not published one. */
    public static LumenGraph empty() {
        return new LumenGraph(new ArrayList<>());
    }

    /** Parses the {@code .graph} topic's JSON; an empty or malformed value gives an empty graph. */
    public static LumenGraph parse(String json) {
        List<Node> nodes = new ArrayList<>();
        try {
            if (json != null && !json.isEmpty()) {
                for (JsonNode n : MAPPER.readTree(json).path("nodes")) {
                    nodes.add(new Node(n.path("id").asInt(), n.path("name").asText(), n.path("kind").asText(),
                            n.path("enabled").asBoolean(true), n.path("input").isNull() || n.path("input").isMissingNode() ? -1 : n.path("input").asInt()));
                }
            }
        } catch (java.io.IOException e) {
            nodes.clear();
        }
        return new LumenGraph(nodes);
    }

    public List<Node> getNodes() {
        return nodes;
    }

    public Optional<Node> findById(int id) {
        return nodes.stream().filter(n -> n.id == id).findFirst();
    }

    /** The camera (or crop) with this name. */
    public Optional<Node> findCamera(String name) {
        return nodes.stream().filter(n -> n.isCamera() && n.name.equals(name)).findFirst();
    }

    /** Every detector fed directly by the given node, in id order. */
    public List<Node> detectorsOf(int inputId) {
        List<Node> out = new ArrayList<>();
        for (Node n : nodes) if (n.isDetector() && n.input == inputId) out.add(n);
        out.sort((a, b) -> Integer.compare(a.id, b.id));
        return out;
    }
}
