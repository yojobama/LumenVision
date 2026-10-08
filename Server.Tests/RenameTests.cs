using System;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class RenameTests
{
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    public void ASourceCannotBeGivenNoName(string? name)
    {
        var ex = Assert.Throws<ApiException>(() => SourceManager.Instance.ChangeSourceName(1, name!));
        Assert.Equal(400, ex.StatusCode);
    }

    [Fact]
    public void RenamingAnUnknownSourceIs404()
    {
        var ex = Assert.Throws<ApiException>(() => SourceManager.Instance.ChangeSourceName(987654, "front"));
        Assert.Equal(404, ex.StatusCode);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("  ")]
    public void ASinkCannotBeGivenNoNameAndKeepsItsOwn(string? name)
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            var ex = Assert.Throws<ApiException>(() => SinkManager.Instance.SetSinkName(id, name!));
            Assert.Equal(400, ex.StatusCode);
            Assert.Equal("preview", SinkManager.Instance.GetSinkById(id).Name);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void RenamingASinkTrimsTheNameAndAnUnknownSinkIs404()
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            SinkManager.Instance.SetSinkName(id, "  stream  ");
            Assert.Equal("stream", SinkManager.Instance.GetSinkById(id).Name);
            var ex = Assert.Throws<ApiException>(() => SinkManager.Instance.SetSinkName(987654, "x"));
            Assert.Equal(404, ex.StatusCode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}

[Collection("ServerSingletons")]
public class PreviewCleanupTests
{
    [Fact]
    public void DeletingANodeDeletesThePreviewsBoundToIt()
    {
        int detector = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        int preview = SinkManager.Instance.AddMjpegSink("tags-preview");
        try
        {
            SinkManager.Instance.BindSourceToSink(preview, detector);

            SinkManager.Instance.DeleteSink(detector);

            Assert.Null(SinkManager.Instance.GetSinkById(preview));
            Assert.Null(SinkManager.Instance.GetSinkById(detector));
        }
        finally
        {
            if (SinkManager.Instance.GetSinkById(preview) != null) SinkManager.Instance.DeleteSink(preview);
            if (SinkManager.Instance.GetSinkById(detector) != null) SinkManager.Instance.DeleteSink(detector);
        }
    }

    [Fact]
    public void OnlyUnboundPreviewsAreSweptAtStart()
    {
        int orphan = SinkManager.Instance.AddMjpegSink("gone-preview");
        int notAPreview = SinkManager.Instance.AddMjpegSink("my stream");
        try
        {
            Assert.True(SinkManager.Instance.DeleteOrphanPreviews() >= 1);

            Assert.Null(SinkManager.Instance.GetSinkById(orphan));
            Assert.NotNull(SinkManager.Instance.GetSinkById(notAPreview));
        }
        finally
        {
            if (SinkManager.Instance.GetSinkById(orphan) != null) SinkManager.Instance.DeleteSink(orphan);
            SinkManager.Instance.DeleteSink(notAPreview);
        }
    }
}

[Collection("ServerSingletons")]
public class MjpegPreviewJanitorTests
{
    [Fact]
    public void AFallbackPreviewNobodyReadsFromIsDeletedAfterTheIdleLimitAndOthersAreKept()
    {
        var janitor = MjpegPreviewJanitor.Instance;
        janitor.Forget();
        int idle = SinkManager.Instance.AddMjpegSink("preview-mjpeg-1");
        int busy = SinkManager.Instance.AddMjpegSink("preview-mjpeg-2");
        int other = SinkManager.Instance.AddMjpegSink("my stream");
        try
        {
            DateTime t0 = DateTime.UtcNow;
            Assert.Equal(0, janitor.Sweep(t0));                                        // first sight starts the clock
            Assert.Equal(0, janitor.Sweep(t0 + TimeSpan.FromSeconds(20)));             // not idle for long enough yet

            MjpegPreviewJanitor.Touch(busy, t0 + TimeSpan.FromSeconds(25));           // a viewer is still reading from this one
            Assert.Equal(1, janitor.Sweep(t0 + TimeSpan.FromSeconds(31)));

            Assert.Null(SinkManager.Instance.GetSinkById(idle));
            Assert.NotNull(SinkManager.Instance.GetSinkById(busy));
            Assert.NotNull(SinkManager.Instance.GetSinkById(other));                   // not a fallback preview: never touched
        }
        finally
        {
            foreach (int id in new[] { idle, busy, other })
                if (SinkManager.Instance.GetSinkById(id) != null) SinkManager.Instance.DeleteSink(id);
            janitor.Forget();
        }
    }
}
