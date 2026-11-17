/*
  SMART SAFE - VOICE ID (speaker voiceprint)  -  ESP32
  The ESP32 makes its own Wi-Fi. The phone opens a page from the ESP32, records the voice,
  turns it into a "voiceprint" (MFCC features: 12 mean + 12 spread numbers) and sends it here.
  The ESP32 compares it with the owner's saved voiceprint. If it is close enough it sends
  3 pulses to the ATmega32 (pin 16) = OPEN. The lock button sends 2 pulses = LOCK.
  The ATmega32 safe program does not need any change for the pulses.

  Wiring (only 2 wires to the safe):
    ESP32 D26 (or D25 / D27) -> ATmega32 pin 16 (PD2)
    ESP32 GND -> safe GND (- rail)
  Power: ESP32 from its own USB. The safe from its own 5V. Do NOT power the safe from ESP32 VIN.

  Phone: Wi-Fi "SmartSafe", password 12345678, then Chrome: http://192.168.4.1
  Chrome needs one setting to allow the microphone on this address (see the guide):
    chrome://flags  ->  "Insecure origins treated as secure"  ->  http://192.168.4.1  -> Enabled -> Relaunch
  Board: ESP32 Dev Module. No extra library needed.
*/
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

const int   SAFE_PINS[] = {25, 26, 27};   // the same pulses go out on D25, D26 and D27: use any one of them -> ATmega32 pin 16
const char *AP_SSID  = "SmartSafe";
const char *AP_PASS  = "12345678";
const int   NF = 24;                      // voiceprint length

WebServer server(80);
Preferences prefs;
float tmpl[NF];                           // owner's voiceprint (average of the enrolled samples)
float enrollSpread = 0;                   // average distance of the enrolled samples to the average
float factor = 1.8;                       // limit = factor x enrollSpread (bigger = easier)
bool  enrolled = false;

void safePulses(int n) {                  // looks like n claps to the ATmega32
  for (int i = 0; i < n; i++) {
    // open-drain: pull the line to 0 V, then let it go. The ATmega32 pull-up makes a clean 5 V HIGH,
    // and the "let go" (rising edge) is what the safe counts. No 3.3 V / 5 V level problem.
    for (int p : SAFE_PINS) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }   delay(150);
    for (int p : SAFE_PINS) pinMode(p, INPUT);                               delay(350);
  }
  Serial.printf("Sent %d pulses on D25/D26/D27\n", n);
}

int parseVec(const String &s, float *v) { // "1.2,3.4,..." -> v[]
  int n = 0, start = 0;
  while (n < NF && start < (int)s.length()) {
    int c = s.indexOf(',', start);
    if (c < 0) c = s.length();
    v[n++] = s.substring(start, c).toFloat();
    start = c + 1;
  }
  return n;
}

float dist(const float *a, const float *b) {
  float d = 0;
  for (int i = 0; i < NF; i++) { float x = a[i] - b[i]; d += x * x; }
  return sqrt(d);
}

float limitNow() { float m = enrollSpread < 1.0 ? 1.0 : enrollSpread; return factor * m; }

void saveAll() {
  prefs.putBytes("tmpl", tmpl, sizeof(tmpl));
  prefs.putFloat("spread", enrollSpread);
  prefs.putFloat("factor", factor);
  prefs.putBool("enr", enrolled);
}

// ---------- the phone page ----------
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Safe Voice ID</title>
<style>
body{font-family:sans-serif;background:#141a2b;color:#eef;margin:0;padding:16px}
h1{font-size:22px;margin:0 0 4px}.s{opacity:.7;font-size:13px;margin-bottom:12px}
.card{background:#1f2740;border-radius:14px;padding:14px;margin:10px 0}
button{width:100%;padding:16px;font-size:18px;border:0;border-radius:12px;margin-top:8px;color:#fff;background:#3a5bd9}
.g{background:#1f9d63}.r{background:#c0392b}.o{background:#7a5af8}
#msg{font-size:20px;font-weight:bold;text-align:center;padding:14px;border-radius:12px;background:#0d1220;min-height:28px}
#bar{height:10px;background:#0d1220;border-radius:6px;overflow:hidden;margin-top:8px}#bar div{height:100%;width:0;background:#ffd166}
input{width:100%}
</style></head><body>
<h1>Smart Safe - Voice ID</h1><div class="s">Say the same sentence every time, e.g. "Open the safe, it is me"</div>
<div id="msg">Ready</div><div id="bar"><div id="lv"></div></div>
<div class="card"><b>1) Owner setup</b> (only once)<br><span class="s">Records the owner 3 times</span>
<button class="o" onclick="enroll()">Record owner voice (3 times)</button>
<div class="s" id="st"></div></div>
<div class="card"><b>2) Open with voice</b>
<button class="g" onclick="verify()">Hold still and speak (2 s)</button>
<button class="r" onclick="go('/lock')">Lock the safe</button></div>
<div class="card"><b>Strictness</b> <span id="fv"></span><br>
<input type="range" min="1.2" max="3" step="0.1" id="f" onchange="setF(this.value)"><div class="s">Left = harder to open, right = easier</div></div>
<script>
const SEC=2.0;let ctx;
function say(t,c){const m=document.getElementById('msg');m.textContent=t;m.style.background=c||'#0d1220'}
async function go(u){const r=await fetch(u);say(await r.text())}
async function status(){const j=await (await fetch('/status')).json();
 document.getElementById('st').textContent=j.enrolled?('Owner saved. Limit: '+j.limit.toFixed(2)):'No owner yet';
 document.getElementById('f').value=j.factor;document.getElementById('fv').textContent=j.factor}
async function setF(v){await fetch('/factor?v='+v);status()}
async function record(){
 if(!navigator.mediaDevices){say('Mic blocked: do the Chrome flag step','#c0392b');throw 0}
 const st=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:false,noiseSuppression:false,autoGainControl:false}});
 ctx=ctx||new (window.AudioContext||window.webkitAudioContext)();await ctx.resume();
 const src=ctx.createMediaStreamSource(st),pr=ctx.createScriptProcessor(4096,1,1),buf=[];
 src.connect(pr);pr.connect(ctx.destination);
 const need=Math.floor(SEC*ctx.sampleRate);let got=0;
 await new Promise(res=>{pr.onaudioprocess=e=>{const d=e.inputBuffer.getChannelData(0);buf.push(new Float32Array(d));got+=d.length;
  let p=0;for(let i=0;i<d.length;i+=8)p=Math.max(p,Math.abs(d[i]));document.getElementById('lv').style.width=Math.min(100,p*200)+'%';
  if(got>=need)res()}});
 pr.disconnect();src.disconnect();st.getTracks().forEach(t=>t.stop());document.getElementById('lv').style.width='0';
 const all=new Float32Array(got);let o=0;for(const b of buf){all.set(b,o);o+=b.length}
 return resample(all,ctx.sampleRate,16000)}
function resample(x,fs,to){const r=fs/to,n=Math.floor(x.length/r),y=new Float32Array(n);
 for(let i=0;i<n;i++){let s=0,a=Math.floor(i*r),b=Math.floor((i+1)*r);for(let k=a;k<b;k++)s+=x[k];y[i]=s/Math.max(1,b-a)}return y}
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]]}}
 for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l;for(let i=0;i<n;i+=l)for(let k=0;k<l/2;k++){const c=Math.cos(a*k),s=Math.sin(a*k),
 tr=re[i+k+l/2]*c-im[i+k+l/2]*s,ti=re[i+k+l/2]*s+im[i+k+l/2]*c;re[i+k+l/2]=re[i+k]-tr;im[i+k+l/2]=im[i+k]-ti;re[i+k]+=tr;im[i+k]+=ti}}}
const FS=16000,N=512,FL=400,HOP=160,NM=26,NC=13;
const mel=f=>2595*Math.log10(1+f/700),imel=m=>700*(Math.pow(10,m/2595)-1);
const fb=(()=>{const lo=mel(80),hi=mel(7600),pts=[];for(let i=0;i<NM+2;i++)pts.push(Math.floor((N+1)*imel(lo+(hi-lo)*i/(NM+1))/FS));
 const F=[];for(let m=1;m<=NM;m++){const w=new Float32Array(N/2+1);for(let k=pts[m-1];k<pts[m];k++)w[k]=(k-pts[m-1])/(pts[m]-pts[m-1]);
 for(let k=pts[m];k<=pts[m+1];k++)w[k]=(pts[m+1]-k)/(pts[m+1]-pts[m]);F.push(w)}return F})();
function voiceprint(x){
 for(let i=x.length-1;i>0;i--)x[i]-=0.97*x[i-1];
 const fr=[],en=[];
 for(let s=0;s+FL<=x.length;s+=HOP){const re=new Float32Array(N),im=new Float32Array(N);let e=0;
  for(let i=0;i<FL;i++){const v=x[s+i]*(0.54-0.46*Math.cos(2*Math.PI*i/(FL-1)));re[i]=v;e+=v*v}
  fft(re,im);const p=new Float32Array(N/2+1);for(let k=0;k<=N/2;k++)p[k]=re[k]*re[k]+im[k]*im[k];
  const lm=fb.map(w=>{let s2=0;for(let k=0;k<w.length;k++)s2+=w[k]*p[k];return Math.log(s2+1e-10)});
  const c=[];for(let j=1;j<NC;j++){let s2=0;for(let m=0;m<NM;m++)s2+=lm[m]*Math.cos(Math.PI*j*(m+0.5)/NM);c.push(s2)}
  fr.push(c);en.push(10*Math.log10(e+1e-12))}
 const mx=Math.max(...en),use=fr.filter((c,i)=>en[i]>mx-30&&en[i]>-55);
 if(use.length<20)return null;
 const v=[];for(let j=0;j<NC-1;j++){let m=0;for(const c of use)m+=c[j];m/=use.length;let s=0;for(const c of use)s+=(c[j]-m)**2;v.push(m,Math.sqrt(s/use.length))}
 return v.map(a=>a.toFixed(3)).join(',')}
async function take(label){say(label+' - speak now','#7a5af8');const x=await record();const v=voiceprint(x);
 if(!v){say('Too quiet, try again closer','#c0392b');throw 0}return v}
async function enroll(){try{const v=[];for(let i=1;i<=3;i++){v.push(await take('Owner sample '+i+'/3'));say('OK '+i+'/3');await new Promise(r=>setTimeout(r,800))}
 const r=await fetch('/enroll',{method:'POST',body:v.join(';')});say(await r.text(),'#1f9d63');status()}catch(e){}}
async function verify(){try{const v=await take('Listening');const j=await (await fetch('/verify',{method:'POST',body:v})).json();
 say((j.ok?'OWNER - OPEN  ':'NOT THE OWNER  ')+'(score '+j.d.toFixed(2)+' / limit '+j.limit.toFixed(2)+')',j.ok?'#1f9d63':'#c0392b')}catch(e){}}
status();
</script></body></html>
)HTML";

void handleEnroll() {
  String body = server.arg("plain");
  float s[3][NF]; int k = 0, start = 0;
  while (k < 3 && start < (int)body.length()) {
    int c = body.indexOf(';', start); if (c < 0) c = body.length();
    if (parseVec(body.substring(start, c), s[k]) == NF) k++;
    start = c + 1;
  }
  if (k < 3) { server.send(400, "text/plain", "Enroll failed, try again"); return; }
  for (int i = 0; i < NF; i++) tmpl[i] = (s[0][i] + s[1][i] + s[2][i]) / 3.0;
  enrollSpread = (dist(s[0], tmpl) + dist(s[1], tmpl) + dist(s[2], tmpl)) / 3.0;
  enrolled = true; saveAll();
  Serial.printf("Enrolled. spread=%.2f limit=%.2f\n", enrollSpread, limitNow());
  server.send(200, "text/plain", "Owner voice saved");
}

void handleVerify() {
  float v[NF];
  String out;
  if (!enrolled) { server.send(200, "application/json", "{\"ok\":false,\"d\":0,\"limit\":0}"); return; }
  if (parseVec(server.arg("plain"), v) != NF) { server.send(400, "application/json", "{\"ok\":false,\"d\":0,\"limit\":0}"); return; }
  float d = dist(v, tmpl), lim = limitNow();
  bool ok = d <= lim;
  Serial.printf("Verify d=%.2f limit=%.2f -> %s\n", d, lim, ok ? "OWNER" : "REJECT");
  out = String("{\"ok\":") + (ok ? "true" : "false") + ",\"d\":" + String(d, 2) + ",\"limit\":" + String(lim, 2) + "}";
  server.send(200, "application/json", out);
  if (ok) safePulses(3);
}

void setup() {
  Serial.begin(115200);
  for (int p : SAFE_PINS) pinMode(p, INPUT);   // idle = let go (the ATmega32 pull-up keeps it HIGH)
  prefs.begin("voiceid", false);
  enrolled = prefs.getBool("enr", false);
  enrollSpread = prefs.getFloat("spread", 0);
  factor = prefs.getFloat("factor", 1.8);
  if (enrolled) prefs.getBytes("tmpl", tmpl, sizeof(tmpl));
  WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", []() { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/enroll", HTTP_POST, handleEnroll);
  server.on("/verify", HTTP_POST, handleVerify);
  server.on("/open", []() { server.send(200, "text/plain", "TEST: opening without voice"); safePulses(3); });   // test only
  server.on("/lock", []() { server.send(200, "text/plain", "Locking"); safePulses(2); });
  server.on("/factor", []() { float f = server.arg("v").toFloat(); if (f >= 1.0 && f <= 4.0) { factor = f; saveAll(); } server.send(200, "text/plain", "ok"); });
  server.on("/status", []() {
    server.send(200, "application/json", String("{\"enrolled\":") + (enrolled ? "true" : "false") +
      ",\"limit\":" + String(limitNow(), 2) + ",\"factor\":" + String(factor, 1) + "}");
  });
  server.begin();
  Serial.println("Voice ID ready: Wi-Fi SmartSafe / 12345678 -> http://192.168.4.1");
}

void loop() { server.handleClient(); }
