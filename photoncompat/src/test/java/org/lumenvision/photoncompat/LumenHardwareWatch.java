package org.lumenvision.photoncompat;

import static org.junit.jupiter.api.Assumptions.assumeTrue;

import edu.wpi.first.networktables.NetworkTableInstance;
import org.junit.jupiter.api.Test;

// Diagnostic for the hardware setup: lists which camera topics exist every few seconds. Run with -Dlumen.hil.watch=<seconds>.
class LumenHardwareWatch {
    @Test
    void watchTopics() throws Exception {
        assumeTrue(System.getProperty("lumen.hil.watch") != null, "set -Dlumen.hil.watch=<seconds>");
        int seconds = Integer.parseInt(System.getProperty("lumen.hil.watch"));
        NetworkTableInstance server = NetworkTableInstance.create();
        server.startServer("", "0.0.0.0", 1735, 5810);
        // topics are only announced to a client that subscribes to them
        edu.wpi.first.networktables.MultiSubscriber all = new edu.wpi.first.networktables.MultiSubscriber(server, new String[] {"/lumenvision/"});
        try {
            for (int t = 0; t < seconds; t += 3) {
                StringBuilder topics = new StringBuilder();
                int nodeTopics = 0;
                for (edu.wpi.first.networktables.TopicInfo info : server.getTopicInfo()) {
                    if (!info.name.contains("/config/")) topics.append(info.name.replace("/lumenvision/", "")).append(' ');
                    if (info.name.contains("/front/") && !info.name.contains("/config/")) nodeTopics++;
                }
                System.out.println("WATCH t=" + t + " clients=" + server.getConnections().length + " nodeTopics=" + nodeTopics + " : " + topics);
                Thread.sleep(3000);
            }
        } finally {
            all.close();
            server.stopServer();
            server.close();
        }
    }
}
