using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class StreamSettingsTests
{
    [Fact]
    public void MjpegSettingsAreAppliedAndRemembered()
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            SinkManager.Instance.SetMjpegSettings(id, 55, 4);

            Sink sink = SinkManager.Instance.GetSinkById(id);
            Assert.Equal(55, sink.StreamJpegQuality);
            Assert.Equal(4, sink.StreamScaleDivisor);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Theory]
    [InlineData(0, 1)]
    [InlineData(101, 1)]
    [InlineData(80, 0)]
    [InlineData(80, 17)]
    public void OutOfRangeMjpegSettingsAreRejected(int quality, int divisor)
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            var ex = Assert.Throws<ApiException>(() => SinkManager.Instance.SetMjpegSettings(id, quality, divisor));
            Assert.Equal(400, ex.StatusCode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void SettingsForTheWrongKindOfSinkAreRejected()
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            Assert.Equal(400, Assert.Throws<ApiException>(() => SinkManager.Instance.SetWebRTCSettings(id, 4000, 30, 1)).StatusCode);
            Assert.Equal(404, Assert.Throws<ApiException>(() => SinkManager.Instance.SetMjpegSettings(987654, 80, 1)).StatusCode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
