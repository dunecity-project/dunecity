import { build } from 'esbuild';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root=fileURLToPath(new URL('../../',import.meta.url));
const temp=await mkdtemp(path.join(tmpdir(),'dunecity-bridge-test-'));
await build({entryPoints:[path.join(root,'platform/web/src/dune-direct-bridge.ts')],outfile:path.join(temp,'bridge.mjs'),bundle:true,platform:'browser',format:'esm'});
const {default:bridge}=await import(pathToFileURL(path.join(temp,'bridge.mjs')));
await rm(temp,{recursive:true,force:true});
const sdp='v=0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=fingerprint:sha-256 '+Array(32).fill('AB').join(':')+'\r\n';
class Channel {
 label='p2pkit';ordered=true;maxRetransmits=null;maxPacketLifeTime=null;
 readyState='connecting';bufferedAmount=0;sent=[];
 close(){this.readyState='closed';this.onclose?.()}
 send(value){this.sent.push(value)}
 open(){this.readyState='open';this.onopen?.()}
}
let pc;
class PC {
 connectionState='new';iceConnectionState='new';iceGatheringState='new';channel=new Channel();stats=new Map();
 constructor(config){this.config=config;pc=this}
 createDataChannel(){return this.channel}
 async createOffer(){return {type:'offer',sdp}}
 async setLocalDescription(d){this.localDescription=d}
 async setRemoteDescription(d){this.remoteDescription=d}
 async addIceCandidate(){}
 async getStats(){return this.stats}
 close(){this.connectionState='closed'}
}
const tick=()=>new Promise(resolve=>setTimeout(resolve,0));
function create(){globalThis.RTCPeerConnection=PC;const handle=bridge.create('["stun:stun.example.org:3478"]',true,'p2pkit');return {handle,pc};}
function deliver(channel,value,id='a'){channel.onmessage({data:JSON.stringify({id,i:0,n:1,part:JSON.stringify(value)})})}
test('game bridge preserves configured STUN and JSON-string wire format',async()=>{
 const {handle,pc}=create();
 try {
  assert.deepEqual(pc.config.iceServers,[{urls:['stun:stun.example.org:3478']}]);
  pc.channel.open();await tick();
  const envelope='g:1:0:0102ff';
  assert.equal(bridge.sendValue(handle,JSON.stringify(envelope)),true);
  await tick();assert.equal(JSON.parse(pc.channel.sent[0]).part,JSON.stringify(envelope));
  deliver(pc.channel,envelope);assert.equal(bridge.pollValue(handle),JSON.stringify(envelope));
  assert.equal(bridge.pollValue(handle),null);
 } finally {bridge.close(handle)}
});
test('game bridge rejects non-string values and closes the underlying connection',async()=>{
 const {handle,pc}=create();
 try {
  pc.channel.open();await tick();deliver(pc.channel,{unexpected:'object'});
  assert.equal(bridge.state(handle),3);assert.equal(pc.connectionState,'closed');
  assert.equal(bridge.pollValue(handle),null);
  assert.equal(bridge.sendValue(handle,'"later"'),false);
  assert.equal(bridge.setRemoteDescription(handle,'offer',sdp),false);
 } finally {bridge.close(handle)}
});
test('closing a bridge cannot be undone by an asynchronous callback',async()=>{
 const {handle,pc}=create();const opened=pc.channel.onopen;
 bridge.close(handle);opened?.();await tick();
 assert.equal(bridge.state(handle),4);assert.equal(pc.connectionState,'closed');
 assert.equal(bridge.pollValue(handle),null);assert.equal(bridge.pollSignal(handle),null);
});

test('a synchronous channel send failure returns false immediately', async()=>{
 const {handle,pc}=create();
 try { pc.channel.open(); await tick();
  pc.channel.send=()=>{throw new Error('forced send failure')};
  assert.equal(bridge.sendValue(handle,'"g:0:0:0102"'),false);
  assert.equal(bridge.state(handle),3);
 } finally {bridge.close(handle)}
});
test('queued messages preserve order and later failures close the bridge',async()=>{
 const {handle,pc}=create();
 try {pc.channel.open(); await tick();pc.channel.bufferedAmount=2<<20;
  assert.equal(bridge.sendValue(handle,'"first"'),true);
  assert.equal(bridge.sendValue(handle,'"second"'),true);
  assert.equal(pc.channel.sent.length,0);
  pc.channel.bufferedAmount=0;await new Promise(r=>setTimeout(r,30));
  assert.deepEqual(pc.channel.sent.map(x=>JSON.parse(JSON.parse(x).part)),['first','second']);
  pc.channel.bufferedAmount=2<<20;assert.equal(bridge.sendValue(handle,'"third"'),true);
  pc.channel.send=()=>{throw new Error('late failure')};pc.channel.bufferedAmount=0;
  await new Promise(r=>setTimeout(r,30));assert.equal(bridge.state(handle),3);
 } finally {bridge.close(handle)}
});

test('failed RTC negotiation keeps a bounded reason instead of a silent disconnect',()=>{
 const {handle,pc}=create();
 try {
  pc.connectionState='failed'; pc.iceConnectionState='failed';
  pc.onconnectionstatechange({type:'connectionstatechange'});
  assert.equal(bridge.state(handle),3);
  assert.match(bridge.lastError(handle),/WebRTC failed; ICE failed/);
  assert.equal(pc.connectionState,'closed');
 } finally {bridge.close(handle)}
});

test('ICE diagnosis records selected types, not candidate addresses or credentials',async()=>{
 const {handle,pc}=create(),warnings=[],previous=console.warn;
 console.warn=(...values)=>warnings.push(values.join(' '));
 try {
  pc.stats=new Map([
   ['transport',{type:'transport',selectedCandidatePairId:'pair'}],
   ['pair',{type:'candidate-pair',localCandidateId:'local',remoteCandidateId:'remote'}],
   ['local',{type:'local-candidate',candidateType:'host',address:'192.0.2.1',port:50000}],
   ['remote',{type:'remote-candidate',candidateType:'srflx',address:'192.0.2.2',port:50001}],
  ]);
  pc.connectionState='failed';pc.iceConnectionState='failed';pc.iceGatheringState='complete';
  pc.onconnectionstatechange({type:'connectionstatechange'});await tick();
  assert.equal(bridge.state(handle),3);
  assert.equal(warnings.length,1);
  assert.match(warnings[0],/phase=failed ice=failed gathering=complete selected_local=host selected_remote=srflx/);
  assert.doesNotMatch(warnings[0],/192\.0\.2\.|50000|50001|fingerprint|candidateId/);
 } finally {console.warn=previous;bridge.close(handle)}
});
