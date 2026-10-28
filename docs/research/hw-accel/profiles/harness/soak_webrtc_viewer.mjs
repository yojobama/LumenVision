// The WebRTC viewer for soak.py, run on a PC on the board's network: a headless Edge or Chrome that watches the soak-webrtc sink for HOURS, reconnects
// when the connection drops or the sink is recreated, and appends one JSON line per minute to LOG (state, frames decoded, bytes, reconnects).
// Usage: node soak_webrtc_viewer.mjs HOURS LOG [BOARD=192.168.68.120:5800] [BROWSER=msedge.exe path]
import { spawn } from 'node:child_process';
import { appendFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const hours = Number(process.argv[2]);
const logFile = process.argv[3];
const board = process.argv[4] ?? '192.168.68.120:5800';
const browserPath = process.argv[5] ?? 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const port = 9335;
const profile = mkdtempSync(join(tmpdir(), 'soak-viewer-'));
const browser = spawn(browserPath, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', 'about:blank'], { stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const log = (o) => appendFileSync(logFile, JSON.stringify({ t: new Date().toISOString(), ...o }) + '\n');

let targets;
for (let i = 0; i < 50 && !targets; i++) {
  try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch { await sleep(200); }
}
const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));
let nextId = 1;
const pending = new Map();
ws.addEventListener('message', (m) => {
  const msg = JSON.parse(m.data);
  if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
});
const send = (method, params = {}) => new Promise((r) => { const id = nextId++; pending.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const evaluate = async (expression) => (await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result?.result?.value;

await send('Page.navigate', { url: `http://${board}/` });
await sleep(3000);

// In the page: keep one connection to the sink named soak-webrtc, reconnecting after 'failed' or 10 s of 'disconnected'
await evaluate(`(() => {
  window.__soak = { reconnects: 0, sinkId: null, pc: null, frames: 0, bytes: 0, down: 0, lastError: null };
  const S = window.__soak;
  const connect = async () => {
    try {
      const sinks = await (await fetch('/api/sink/getAll')).json();
      const sink = sinks.find((s) => (s.Name ?? s.name) === 'soak-webrtc');
      if (!sink) { S.lastError = 'no soak-webrtc sink'; return; }
      S.sinkId = sink.Id ?? sink.id;
      if (S.pc) S.pc.close();
      const peer = 'soak-viewer-' + Math.random().toString(36).slice(2, 8);
      const pc = new RTCPeerConnection({ iceServers: [] });
      S.pc = pc;
      let answered = false; const queued = [];
      const post = (c) => fetch('/api/webrtcSink/candidate?' + new URLSearchParams({ sinkId: S.sinkId, candidate: c.candidate, mid: c.sdpMid || '', peer }), { method: 'POST' });
      pc.onicecandidate = (e) => { if (!e.candidate || !e.candidate.candidate) return; if (!answered) queued.push(e.candidate); else post(e.candidate); };
      const offer = await (await fetch('/api/webrtcSink/offer?sinkId=' + S.sinkId + '&peer=' + peer, { method: 'POST' })).text();
      await pc.setRemoteDescription({ type: 'offer', sdp: offer });
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      await fetch('/api/webrtcSink/answer?sinkId=' + S.sinkId + '&peer=' + peer, { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: answer.sdp });
      answered = true;
      for (const c of queued) await post(c);
    } catch (e) { S.lastError = String(e); }
  };
  S.connect = connect;
  setInterval(async () => {
    const pc = S.pc;
    if (!pc || pc.connectionState === 'failed' || pc.connectionState === 'closed') { S.down += 2; if (S.down >= 10) { S.down = 0; S.reconnects++; connect(); } return; }
    if (pc.connectionState === 'disconnected') { S.down += 2; if (S.down >= 10) { S.down = 0; S.reconnects++; connect(); } return; }
    S.down = 0;
  }, 2000);
  connect();
})()`);

const end = Date.now() + hours * 3600 * 1000;
let prev = { frames: 0, bytes: 0 };
log({ event: 'viewer started', board, hours });
while (Date.now() < end) {
  await sleep(60000);
  const r = await evaluate(`(async () => {
    const S = window.__soak; const pc = S.pc; let frames = 0, bytes = 0, fps = null, jitter = null, lost = 0, freezes = null;
    if (pc) (await pc.getStats()).forEach((v) => { if (v.type === 'inbound-rtp' && v.kind === 'video') { frames = v.framesDecoded ?? 0; bytes = v.bytesReceived ?? 0; fps = v.framesPerSecond ?? null; jitter = v.jitter ?? null; lost = v.packetsLost ?? 0; freezes = v.freezeCount ?? null; } });
    return { state: pc ? pc.connectionState : 'none', sinkId: S.sinkId, reconnects: S.reconnects, frames, bytes, fps, jitter, lost, freezes, lastError: S.lastError };
  })()`);
  if (!r) { log({ error: 'page evaluation failed' }); continue; }
  // a reconnect starts the counters again
  const dFrames = r.frames >= prev.frames ? r.frames - prev.frames : r.frames;
  const dBytes = r.bytes >= prev.bytes ? r.bytes - prev.bytes : r.bytes;
  prev = { frames: r.frames, bytes: r.bytes };
  log({ state: r.state, sinkId: r.sinkId, reconnects: r.reconnects, framesPerMin: dFrames, kbPerMin: Math.round(dBytes / 1024), fps: r.fps, jitter: r.jitter, lost: r.lost, freezes: r.freezes, lastError: r.lastError });
}
log({ event: 'viewer finished' });
ws.close();
browser.kill();
