using Server;
using Server.Controllers.sinks;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class WebRtcSignallingTests
{
    // Firefox posts an empty end-of-candidates marker after its real candidates; the query binder hands it (and an empty mid) to the controller as
    // null, which used to reach the native call and fail every preview's signalling with "ArgumentNullException: null string"
    [Theory]
    [InlineData("", "video")]
    [InlineData(null, "video")]
    [InlineData(null, null)]
    public void TheEndOfCandidatesMarkerIsAcceptedAndIgnored(string? candidate, string? mid)
    {
        int id = SinkManager.Instance.AddWebRTCSink("preview", 4000, 30, ManagerWrapper.Instance.GetPreferredWebRTCEncoder());
        try
        {
            Task result = new WebRTCSinkController().AddIceCandidate(id, candidate, mid, "viewer");
            Assert.True(result.IsCompletedSuccessfully);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
