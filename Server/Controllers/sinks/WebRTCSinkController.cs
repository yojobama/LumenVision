using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    // REST WebRTC signalling with non-trickle ICE on the server side (the offer is returned once gathering completes);
    // the browser's candidates are added as they arrive via /webrtcSink/candidate.
    internal class WebRTCSinkController : ControllerBase
    {
        // POST: create a WebRTCSink; bind it afterwards (PATCH /sink/bind). encoderName null = this build/board's preferred encoder.
        // bitrateKbps/fps are nullable and defaulted (4000/30) in the body via ??, since an omitted [FromQuery] value type binds 0.
        [HttpPost("webrtcSink/create")]
        public Task<int> Create([FromQuery] string name, [FromQuery] int? bitrateKbps = null,
            [FromQuery] int? fps = null, [FromQuery] string? encoderName = null)
        {
            string resolvedEncoderName = encoderName ?? ManagerWrapper.Instance.GetPreferredWebRTCEncoder();
            int sinkId = SinkManager.Instance.AddWebRTCSink(name, bitrateKbps ?? 4000, fps ?? 30, resolvedEncoderName);
            return Task.FromResult(sinkId);
        }

        // POST: get an SDP offer (blocks briefly for ICE gathering), written as raw text/plain since SDP contains literal \r\n.
        [HttpPost("webrtcSink/offer")]
        public async Task CreateOffer([FromQuery] int sinkId)
        {
            string sdp = SinkManager.Instance.WebRTCCreateOffer(sinkId);
            await HttpContext.SendStringAsync(sdp, "text/plain", Encoding.UTF8);
        }

        // POST: submit the browser's SDP answer
        [HttpPost("webrtcSink/answer")]
        public async Task SetAnswer([FromQuery] int sinkId)
        {
            using var reader = new StreamReader(HttpContext.OpenRequestStream());
            string sdp = await reader.ReadToEndAsync();
            SinkManager.Instance.WebRTCSetAnswer(sinkId, sdp);
        }

        // POST: submit one of the browser's trickled ICE candidates
        [HttpPost("webrtcSink/candidate")]
        public Task AddIceCandidate([FromQuery] int sinkId, [FromQuery] string candidate, [FromQuery] string mid)
        {
            SinkManager.Instance.WebRTCAddIceCandidate(sinkId, candidate, mid);
            return Task.CompletedTask;
        }

        // GET: connection status
        [HttpGet("webrtcSink/status")]
        public Task<WebRtcStatusDto> GetStatus([FromQuery] int sinkId)
        {
            return Task.FromResult(WebRtcStatusDto.Parse(SinkManager.Instance.GetWebRTCSinkStatus(sinkId)));
        }

        // GET: the encoder /webrtcSink/create picks when encoderName is unset ("h264_rkmpp" with hardware ffmpeg, else "libx264")
        [HttpGet("webrtcSink/preferredEncoder")]
        public Task<string> GetPreferredEncoder()
        {
            return Task.FromResult(ManagerWrapper.Instance.GetPreferredWebRTCEncoder());
        }
    }
}
