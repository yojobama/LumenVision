namespace Server
{
    // Saves/restores the whole node graph (every source, sink and binding) under a name by reusing DB.Save()/DB.Load().
    // Distinct from the per-source PipelineProfile.
    public class GraphProfile
    {
        public static GraphProfile Instance { get; } = new GraphProfile();

        private const string ProfilesDir = "graph-profiles";
        private const string DataJsonPath = "data.json";

        private GraphProfile() { }

        private static string PathFor(string name) => Path.Combine(ProfilesDir, SanitizeName(name) + ".json");

        // profile names become file names: reject anything that isn't a plain path segment so it can't escape ProfilesDir
        private static string SanitizeName(string name)
        {
            if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("Graph profile name must not be empty");
            if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.Contains(".."))
                throw new ArgumentException($"Invalid graph profile name: {name}");
            return name;
        }

        public List<string> ListProfiles()
        {
            if (!Directory.Exists(ProfilesDir)) return new List<string>();
            return Directory.GetFiles(ProfilesDir, "*.json")
                .Select(Path.GetFileNameWithoutExtension)
                .Where(n => n != null)
                .Select(n => n!)
                .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        // Save() first so the snapshot reflects the live state (DB.Save re-reads Manager's current state).
        public void SaveCurrentAs(string name)
        {
            Directory.CreateDirectory(ProfilesDir);
            DB.Instance.Save();
            File.Copy(DataJsonPath, PathFor(name), overwrite: true);
        }

        // Deletes every live sink, then every source, then rebuilds the graph from the snapshot via DB.Load() (the startup path).
        public void Activate(string name)
        {
            string path = PathFor(name);
            if (!File.Exists(path)) throw new FileNotFoundException($"No saved graph profile named '{name}'", path);

            foreach (int sinkId in SinkManager.Instance.getAllSinkIds().ToList())
            {
                SinkManager.Instance.DeleteSink(sinkId);
            }
            foreach (int sourceId in SourceManager.Instance.GetAllSourceIds().ToList())
            {
                SourceManager.Instance.DeleteSource(sourceId);
            }

            File.Copy(path, DataJsonPath, overwrite: true);
            DB.Instance.Load();
        }
    }
}
