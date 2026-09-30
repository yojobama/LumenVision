using System;
using System.IO;
using System.Linq;
using Server;
using Xunit;

namespace Server.Tests;

public class SnapshotTests : IDisposable
{
    private readonly string _camera = "testcam-" + Guid.NewGuid().ToString("N")[..8];

    private string Write(string fileName)
    {
        string dir = Path.Combine(SnapshotService.SnapshotRoot, _camera);
        Directory.CreateDirectory(dir);
        File.WriteAllBytes(Path.Combine(dir, fileName), new byte[] { 1, 2, 3 });
        return $"{_camera}/{fileName}";
    }

    public void Dispose()
    {
        string dir = Path.Combine(SnapshotService.SnapshotRoot, _camera);
        if (Directory.Exists(dir)) Directory.Delete(dir, true);
    }

    [Fact]
    public void ListedSnapshotsCarryTheirCameraAndKind()
    {
        string input = Write("20260101-000000-000-input.jpg");
        string output = Write("20260101-000001-000-output.jpg");

        var entries = SnapshotService.Instance.List().Where(e => e.Camera == _camera).ToArray();

        Assert.Equal(2, entries.Length);
        Assert.Equal("output", entries.Single(e => e.Path == output).Kind);
        Assert.Equal("input", entries.Single(e => e.Path == input).Kind);
        Assert.All(entries, e => Assert.Equal(3, e.SizeBytes));
    }

    [Fact]
    public void OnlyJpegsInsideTheSnapshotRootResolve()
    {
        string path = Write("a-input.jpg");

        Assert.NotNull(SnapshotService.Instance.ResolveFile(path));
        Assert.Null(SnapshotService.Instance.ResolveFile($"{_camera}/missing.jpg"));
        Assert.Null(SnapshotService.Instance.ResolveFile("../Server.dll"));
        Assert.Null(SnapshotService.Instance.ResolveFile($"{_camera}/../../Server.dll"));
        Assert.Null(SnapshotService.Instance.ResolveFile(Path.GetFullPath(Path.Combine(SnapshotService.SnapshotRoot, "..", "x.jpg"))));
        Assert.Null(SnapshotService.Instance.ResolveFile(""));
    }

    [Fact]
    public void DeleteRemovesTheFileOnce()
    {
        string path = Write("b-input.jpg");

        Assert.True(SnapshotService.Instance.Delete(path));
        Assert.False(SnapshotService.Instance.Delete(path));
    }
}
