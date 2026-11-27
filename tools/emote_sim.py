#!/usr/bin/env python3
"""Render the orion-fc flip emote as a 3D scene.

What is firmware and what is model:

  profile (rate setpoint vs time, thrust scale)  -> parsed from Emote.hpp
  airframe response                              -> MODEL, adjustable tau

The page integrates the profile through a first-order rate response so the
"actual rate"/angle readouts are explicitly a model, not firmware. three.js is
vendored into build/vendor on first run so the page works offline.

    python3 tools/emote_sim.py      # writes build/flip3d.html
"""
import json
import pathlib
import re
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "lib/Espfc/src/Control/Emote.hpp"
BUILD = ROOT / "build"
VENDOR = BUILD / "vendor"
OUT = BUILD / "flip3d.html"

CDN = {
    "three.module.js": "https://unpkg.com/three@0.160.0/build/three.module.js",
    "OrbitControls.js": "https://unpkg.com/three@0.160.0/examples/jsm/controls/OrbitControls.js",
}

# Motor mapping from the firmware target header.
OUTPUT_GPIO = {0: 6, 1: 20, 2: 21, 3: 22}
# Betaflight positions, and the spin direction confirmed on the bench.
MOTORS = [
    {"m": 1, "idx": 0, "pos": "rear right",  "cw": True},
    {"m": 2, "idx": 1, "pos": "front right", "cw": False},
    {"m": 3, "idx": 2, "pos": "rear left",   "cw": False},
    {"m": 4, "idx": 3, "pos": "front left",  "cw": True},
]


def const(name, cast=float):
    m = re.search(rf"{name}\s*=\s*([0-9.]+)f?;", HEADER.read_text())
    if not m:
        raise SystemExit(f"constant {name} not found in {HEADER}")
    return cast(float(m.group(1)))


RAMP = const("RAMP_MS", int)
HOLD = const("FLIP_HOLD_MS", int)
DECEL = const("DECEL_MS", int)
BLIP = const("BLIP_MS", int)
PEAK = const("FLIP_PEAK_DPS")
THRUST = const("THRUST_SCALE")
HOLD_180 = const("ROLL180_HOLD_MS", int)
TOTAL = RAMP + HOLD + DECEL + BLIP
TOTAL_180 = RAMP + HOLD_180 + DECEL + BLIP


def profile(kind, t_ms, direction=1):
    """Deg/s setpoint and thrust scale, straight from the firmware shape."""
    hold = HOLD_180 if kind == "roll180" else HOLD
    total = TOTAL_180 if kind == "roll180" else TOTAL
    if t_ms >= total:
        return 0.0, 1.0
    hold_end = RAMP + hold
    decel_end = hold_end + DECEL
    if t_ms < RAMP:
        rate = PEAK * t_ms / RAMP
    elif t_ms < hold_end:
        rate = PEAK
    elif t_ms < decel_end:
        rate = PEAK * (1 - (t_ms - hold_end) / DECEL)
    else:
        rate = -PEAK * const("BLIP_FACTOR")
    return rate * direction, THRUST


def series(kind, direction=1):
    total = TOTAL_180 if kind == "roll180" else TOTAL
    return [{"t": t, "cmd": round(profile(kind, t, direction)[0], 2)}
            for t in range(total)]


def ensure_vendor():
    VENDOR.mkdir(parents=True, exist_ok=True)
    for name, url in CDN.items():
        dest = VENDOR / name
        if dest.exists() and dest.stat().st_size > 1000:
            continue
        print(f"fetching {name}")
        urllib.request.urlretrieve(url, dest)


def main():
    data = {}
    for kind in ("flip", "roll180"):
        for direction, label in ((1, "right"), (-1, "left")):
            data[f"{kind}-{label}"] = series(kind, direction)
    ensured = {"flip": TOTAL, "roll180": TOTAL_180, "peak": PEAK,
               "ramp": RAMP, "hold": HOLD, "hold180": HOLD_180,
               "decel": DECEL, "blip": BLIP, "thrust": THRUST,
               "motors": [{**m, "gpio": OUTPUT_GPIO[m["idx"]]} for m in MOTORS]}
    ensure_vendor()
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(PAGE.replace("__DATA__", json.dumps(data))
                       .replace("__CONST__", json.dumps(ensured)))
    print(f"wrote {OUT}")
    for kind in ("flip", "roll180"):
        print(f"  {kind}: {ensured[kind]} ms profile, peak {PEAK:.0f} deg/s")


PAGE = r"""<!doctype html>
<html><head><meta charset="utf-8"><title>orion-fc flip emote &mdash; 3D</title>
<style>
 :root{color-scheme:dark}
 body{margin:0;background:#0d0f13;color:#e7e9ee;font:13px/1.45 system-ui,-apple-system,sans-serif;overflow:hidden}
 #app{position:fixed;inset:0}
 #hud{position:fixed;left:18px;top:16px;pointer-events:none}
 h1{margin:0 0 3px;font-size:15px;font-weight:600}
 .sub{color:#7d8492;font-size:11.5px;margin-bottom:12px}
 .kv{display:grid;grid-template-columns:124px 100px;gap:3px 10px;font-variant-numeric:tabular-nums}
 .k{color:#7d8492}
 .fw{color:#30d158} .md{color:#e8c547}
 #bar{position:fixed;left:18px;bottom:16px;display:flex;gap:7px;flex-wrap:wrap;align-items:center}
 button{background:#1b1f27;color:#dfe3ea;border:1px solid #2c323d;border-radius:8px;
        padding:7px 12px;cursor:pointer;font:inherit}
 button:hover{background:#232936}
 button.on{background:#2f6df6;border-color:#2f6df6;color:#fff}
 #graph{position:fixed;right:16px;bottom:16px;background:rgba(15,18,24,.75);
        border:1px solid #232936;border-radius:10px}
 .hint{position:fixed;right:16px;top:16px;color:#5f6673;font-size:11.5px;text-align:right;line-height:1.6}
 .lbl{position:absolute;transform:translate(-50%,-50%);font:11px/1.25 ui-monospace,monospace;
      padding:3px 6px;border-radius:6px;background:rgba(16,20,26,.88);
      border:1px solid #2c323d;white-space:nowrap;pointer-events:none;color:#cfd5df}
 .lbl b{color:#fff;font-weight:600}
 .tau{display:flex;align-items:center;gap:8px;color:#7d8492;font-size:12px}
 input[type=range]{width:110px;accent-color:#2f6df6}
</style></head><body>
<div id="app"></div>
<div id="hud">
  <h1>orion-fc &mdash; 360 flip</h1>
  <div class="sub">roll about the longitudinal axis &middot; <span class="fw">profile: firmware</span> &middot; <span class="md">response: model</span></div>
  <div class="kv">
    <span class="k">profile time</span><span id="v_t"></span>
    <span class="k">commanded rate</span><span id="v_cmd"></span>
    <span class="k">modelled rate</span><span id="v_rate"></span>
    <span class="k">roll angle</span><span id="v_angle"></span>
    <span class="k">thrust scale</span><span id="v_thr"></span>
    <span class="k">modelled final</span><span id="v_final"></span>
  </div>
</div>
<div id="bar">
  <button id="bf" class="on">360 flip</button><button id="b180">180 roll</button>
  <button id="br" class="on">right</button><button id="bl">left</button>
  <button id="bp">&#9654; replay</button>
  <button id="blab" class="on">motor labels</button>
  <span class="tau">&tau; <input id="tau" type="range" min="0" max="120" step="5" value="45"><span id="tauv">45 ms</span></span>
</div>
<canvas id="graph" width="330" height="120"></canvas>
<div class="hint">drag to orbit &middot; scroll to zoom<br>roll = left/right props differ</div>

<script type="importmap">
{"imports":{"three":"./vendor/three.module.js","three/addons/":"./vendor/"}}
</script>
<script type="module">
import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';

const DATA = __DATA__, C = __CONST__;
const $ = id => document.getElementById(id);

// ---------- renderer / scene ----------
const renderer = new THREE.WebGLRenderer({antialias:true});
renderer.setPixelRatio(Math.min(devicePixelRatio,2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
$('app').appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0d0f13);
scene.fog = new THREE.Fog(0x0d0f13, 15, 36);

const camera = new THREE.PerspectiveCamera(42, innerWidth/innerHeight, 0.1, 200);
camera.position.set(6.4, 4.3, 6.9);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 0.9, 0);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.minDistance = 3;
controls.maxDistance = 22;
controls.maxPolarAngle = Math.PI * 0.495;

scene.add(new THREE.HemisphereLight(0xbfd4ff, 0x1a1f27, 1.15));
const key = new THREE.DirectionalLight(0xffffff, 2.1);
key.position.set(5, 9, 4); key.castShadow = true;
key.shadow.mapSize.set(1024,1024);
key.shadow.camera.left = -8; key.shadow.camera.right = 8;
key.shadow.camera.top = 8; key.shadow.camera.bottom = -8;
key.shadow.camera.near = 1; key.shadow.camera.far = 30;
scene.add(key);
const rim = new THREE.DirectionalLight(0x4d7cff, 0.8);
rim.position.set(-6, 3, -5); scene.add(rim);

const ground = new THREE.Mesh(new THREE.CircleGeometry(16, 64),
  new THREE.MeshStandardMaterial({color:0x141821, roughness:1}));
ground.rotation.x = -Math.PI/2; ground.receiveShadow = true; scene.add(ground);
const grid = new THREE.GridHelper(32, 32, 0x222836, 0x1a1f29);
grid.position.y = 0.002; scene.add(grid);

// ---------- airframe ----------
// local frame: +X nose (roll axis), +Y up, +Z right
const quad = new THREE.Group();
quad.position.y = 0.95;
scene.add(quad);

const bodyMat  = new THREE.MeshStandardMaterial({color:0x2f6df6, roughness:0.45, metalness:0.25});
const armMat   = new THREE.MeshStandardMaterial({color:0x2a303c, roughness:0.7});
const motorMat = new THREE.MeshStandardMaterial({color:0x9aa4b4, roughness:0.35, metalness:0.7});

const body = new THREE.Mesh(new THREE.BoxGeometry(1.5, 0.26, 1.5), bodyMat);
body.castShadow = true; quad.add(body);
const canopy = new THREE.Mesh(
  new THREE.SphereGeometry(0.42, 20, 12, 0, Math.PI*2, 0, Math.PI/2),
  new THREE.MeshStandardMaterial({color:0x1b2230, roughness:0.3}));
canopy.position.y = 0.1; canopy.scale.set(1,0.55,1); quad.add(canopy);

const ARM = 1.35;
// scene corner for each Betaflight position (metres, local)
const cornerFor = pos => ({
  "front right": [ ARM,  ARM],
  "front left":  [ ARM, -ARM],
  "rear right":  [-ARM,  ARM],
  "rear left":   [-ARM, -ARM],
}[pos]);

const rotors = [], labelAnchors = [];
const labelLayer = document.createElement('div');
labelLayer.style.cssText = 'position:fixed;inset:0;pointer-events:none';
document.body.appendChild(labelLayer);

C.motors.forEach(m => {
  const [x, z] = cornerFor(m.pos);

  const arm = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.12, 1.15), armMat);
  arm.position.set(x*0.5, 0, z*0.5);
  arm.rotation.y = Math.atan2(x, z);
  quad.add(arm);

  const motor = new THREE.Mesh(new THREE.CylinderGeometry(0.17,0.19,0.16,16), motorMat);
  motor.position.set(x, 0.08, z); motor.castShadow = true; quad.add(motor);

  const hub = new THREE.Group();
  hub.position.set(x, 0.2, z); quad.add(hub);

  const disc = new THREE.Mesh(new THREE.CylinderGeometry(0.52,0.52,0.012,32),
    new THREE.MeshStandardMaterial({color: m.cw ? 0x49b265 : 0x4d7cff,
      transparent:true, opacity:0.32, side:THREE.DoubleSide}));
  disc.position.y = 0.01; hub.add(disc);
  for (let b = 0; b < 2; b++) {
    const blade = new THREE.Mesh(new THREE.BoxGeometry(0.98, 0.02, 0.16),
      new THREE.MeshStandardMaterial({color: m.cw ? 0x6fe08c : 0x7ea6ff, roughness:0.4}));
    blade.rotation.y = b * Math.PI/2;
    hub.add(blade);
  }
  rotors.push({hub, side: z > 0 ? 'right' : 'left', cw: m.cw});

  const el = document.createElement('div');
  el.className = 'lbl';
  el.innerHTML = `<b>M${m.m}</b> GPIO${m.gpio}<br>${m.pos} &middot; ${m.cw ? 'CW' : 'CCW'}`;
  labelLayer.appendChild(el);
  labelAnchors.push({el, object: hub});
});

const nose = new THREE.Mesh(new THREE.ConeGeometry(0.17, 0.42, 12),
  new THREE.MeshStandardMaterial({color:0xe8c547, roughness:0.4}));
nose.rotation.z = -Math.PI/2; nose.position.set(0.95, 0.02, 0); quad.add(nose);

const ledR = new THREE.Mesh(new THREE.SphereGeometry(0.075,12,10),
  new THREE.MeshStandardMaterial({color:0xff3b30, emissive:0xff3b30, emissiveIntensity:1.6}));
ledR.position.set(-0.7, 0.03, 0.7); quad.add(ledR);
const ledG = new THREE.Mesh(new THREE.SphereGeometry(0.075,12,10),
  new THREE.MeshStandardMaterial({color:0x30d158, emissive:0x30d158, emissiveIntensity:1.6}));
ledG.position.set(-0.7, 0.03, -0.7); quad.add(ledG);

const axis = new THREE.Mesh(new THREE.CylinderGeometry(0.008,0.008,4.8,8),
  new THREE.MeshBasicMaterial({color:0x2b3342}));
axis.rotation.z = -Math.PI/2; quad.add(axis);

// ---------- model: first-order airframe response ----------
let tau = 45;                       // ms, adjustable: this is the MODEL part
let modelRate = [], modelAngle = [], modelFinal = 0;

function rebuildModel(){
  const rows = DATA[kind + '-' + dirn];
  modelRate = new Array(rows.length); modelAngle = new Array(rows.length);
  let rate = 0, angle = 0;
  for (let i = 0; i < rows.length; i++){
    const cmd = rows[i].cmd;
    if (tau <= 0) rate = cmd;
    else rate += (cmd - rate) * (1 / tau);     // 1 ms steps
    angle += rate / 1000;
    modelRate[i] = rate; modelAngle[i] = angle;
  }
  modelFinal = angle;
}

// ---------- playback ----------
let kind = 'flip', dirn = 'right', simT = 0, speed = 0.5, playing = true;
const sel = (id,on)=>$(id).classList.toggle('on',on);
const total = () => C[kind];

function restart(){ simT = 0; rebuildModel(); }
rebuildModel();

$('bf').onclick = ()=>{kind='flip';sel('bf',true);sel('b180',false);restart()};
$('b180').onclick = ()=>{kind='roll180';sel('b180',true);sel('bf',false);restart()};
$('br').onclick = ()=>{dirn='right';sel('br',true);sel('bl',false);restart()};
$('bl').onclick = ()=>{dirn='left';sel('bl',true);sel('br',false);restart()};
$('bp').onclick = ()=>{simT=0;playing=true};
$('tau').oninput = e => { tau = +e.target.value; $('tauv').textContent = tau + ' ms'; restart(); };
$('blab').onclick = () => {
  const on = !$('blab').classList.contains('on');
  sel('blab', on);
  labelLayer.style.display = on ? 'block' : 'none';
};

const g = $('graph').getContext('2d');
function drawGraph(i){
  const rows = DATA[kind + '-' + dirn], W = 330, H = 120;
  g.clearRect(0,0,W,H);
  g.strokeStyle='#232936'; g.beginPath(); g.moveTo(0,H/2); g.lineTo(W,H/2); g.stroke();
  const n = rows.length, rmax = Math.max(200, ...rows.map(r=>Math.abs(r.cmd)));
  const y = v => H/2 - (v/rmax)*(H/2-14);
  g.strokeStyle='#495366'; g.lineWidth=1; g.beginPath();
  rows.forEach((r,k)=>{const x=k/n*W; k?g.lineTo(x,y(r.cmd)):g.moveTo(x,y(r.cmd))});
  g.stroke();
  g.strokeStyle='#e8c547'; g.lineWidth=1.7; g.beginPath();
  modelRate.forEach((v,k)=>{const x=k/n*W; k?g.lineTo(x,y(v)):g.moveTo(x,y(v))});
  g.stroke();
  g.fillStyle='#e7e9ee'; g.fillRect(i/n*W, 6, 1.5, H-12);
  g.fillStyle='#7d8492'; g.font='10.5px system-ui';
  g.fillText('rate: firmware command / modelled response', 8, 13);
}

const v3 = new THREE.Vector3();
let last = performance.now(), spin = 0;

function tick(now){
  // rAF can hand back a timestamp older than the startup value of `last`,
  // which would make the delta negative and the index out of range.
  const dtWall = Math.max(0, Math.min((now - last)/1000, 0.05)); last = now;
  const totalMs = total();
  if (playing){
    simT += dtWall * 1000 * speed;
    if (simT > totalMs + 400) simT = 0;
  }
  const rows = DATA[kind + '-' + dirn];
  const i = Math.max(0, Math.min(Math.floor(simT), rows.length - 1));
  const cmd = rows[i].cmd, rate = modelRate[i], angle = modelAngle[i];

  quad.rotation.x = THREE.MathUtils.degToRad(angle);

  // a roll speeds one side and slows the other; yaw would differ diagonally
  const bias = Math.max(-1, Math.min(1, rate / C.peak));
  spin += dtWall * 55;
  rotors.forEach(rt => {
    const k = rt.side === 'right' ? 1 - 0.55*bias : 1 + 0.55*bias;
    rt.hub.rotation.y = spin * k * (rt.cw ? -1 : 1);
  });

  $('v_t').textContent      = simT.toFixed(0) + ' / ' + totalMs + ' ms';
  $('v_cmd').textContent    = cmd.toFixed(0) + ' deg/s';
  $('v_rate').textContent   = rate.toFixed(0) + ' deg/s';
  $('v_angle').textContent  = angle.toFixed(0) + ' deg';
  $('v_thr').textContent    = (simT < totalMs ? 'x' + C.thrust.toFixed(2) : 'x1.00');
  $('v_final').textContent  = modelFinal.toFixed(0) + ' deg';

  labelAnchors.forEach(({el, object}) => {
    object.getWorldPosition(v3); v3.project(camera);
    const visible = v3.z < 1;
    el.style.opacity = visible ? 1 : 0;
    el.style.left = ((v3.x*0.5+0.5)*innerWidth) + 'px';
    el.style.top  = ((-v3.y*0.5+0.5)*innerHeight - 34) + 'px';
  });

  drawGraph(i);
  controls.update();
  renderer.render(scene, camera);
  requestAnimationFrame(tick);
}

addEventListener('resize', ()=>{
  camera.aspect = innerWidth/innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});
requestAnimationFrame(tick);
</script></body></html>
"""

if __name__ == "__main__":
    main()
