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
