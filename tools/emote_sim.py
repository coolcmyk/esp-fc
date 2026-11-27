#!/usr/bin/env python3
"""Render the orion-fc flip emote as a 3D scene.

The profile constants are parsed from the firmware header, so the picture cannot
drift from what the FC commands. three.js is vendored into build/vendor on first
run so the page also works offline.

    python3 tools/emote_sim.py            # writes build/flip3d.html
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

TAU = 0.045   # s, first-order rate response of the airframe
DT = 0.001    # s


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
BLIP_F = const("BLIP_FACTOR")
HOLD_180 = const("ROLL180_HOLD_MS", int)
TOTAL = RAMP + HOLD + DECEL + BLIP
TOTAL_180 = RAMP + HOLD_180 + DECEL + BLIP


def profile(kind, t_ms, direction=1):
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
        rate = -PEAK * BLIP_F
    return rate * direction, THRUST


def simulate(kind, direction=1):
    total = TOTAL_180 if kind == "roll180" else TOTAL
    rate = angle = 0.0
    rows = []
    for i in range(int(total / (DT * 1000))):
        t_ms = i * DT * 1000
        cmd, thr = profile(kind, t_ms, direction)
        rate += (cmd - rate) * (DT / TAU)
        angle += rate * DT
        rows.append({"t": round(t_ms, 1), "cmd": round(cmd, 2), "rate": round(rate, 2),
                     "angle": round(angle, 2), "thr": round(thr, 3)})
    return rows, angle


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
            rows, final = simulate(kind, direction=direction)
            data[f"{kind}-{label}"] = {"rows": rows, "final": round(final, 1)}
            print(f"{kind:8s} {label:5s}: {len(rows)} ms, final angle {final:7.1f} deg")

    ensure_vendor()
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(PAGE.replace("__DATA__", json.dumps(data)))
    print(f"wrote {OUT}  (open it, or serve the folder)")


PAGE = r"""<!doctype html>
<html><head><meta charset="utf-8"><title>orion-fc flip emote &mdash; 3D</title>
<style>
 :root{color-scheme:dark}
 body{margin:0;background:#0d0f13;color:#e7e9ee;font:13px/1.45 system-ui,-apple-system,sans-serif;overflow:hidden}
 #app{position:fixed;inset:0}
 #hud{position:fixed;left:18px;top:16px;pointer-events:none}
 h1{margin:0 0 2px;font-size:15px;font-weight:600;letter-spacing:.2px}
 .sub{color:#7d8492;font-size:11.5px;margin-bottom:12px}
 .kv{display:grid;grid-template-columns:118px 92px;gap:3px 10px;font-variant-numeric:tabular-nums}
 .k{color:#7d8492}
 #bar{position:fixed;left:18px;bottom:16px;display:flex;gap:7px;flex-wrap:wrap}
 button{background:#1b1f27;color:#dfe3ea;border:1px solid #2c323d;border-radius:8px;
        padding:7px 12px;cursor:pointer;font:inherit}
 button:hover{background:#232936}
 button.on{background:#2f6df6;border-color:#2f6df6;color:#fff}
 #graph{position:fixed;right:16px;bottom:16px;background:rgba(15,18,24,.75);
        border:1px solid #232936;border-radius:10px}
 .hint{position:fixed;right:16px;top:16px;color:#5f6673;font-size:11.5px;text-align:right}
</style></head><body>
<div id="app"></div>
<div id="hud">
  <h1>orion-fc &mdash; 360 flip</h1>
  <div class="sub">roll about the longitudinal axis &middot; left/right props differ</div>
  <div class="kv">
    <span class="k">time</span><span id="v_t"></span>
    <span class="k">commanded rate</span><span id="v_cmd"></span>
    <span class="k">actual rate</span><span id="v_rate"></span>
    <span class="k">roll angle</span><span id="v_angle"></span>
    <span class="k">thrust</span><span id="v_thr"></span>
    <span class="k">final angle</span><span id="v_final"></span>
  </div>
</div>
<div id="bar">
  <button id="bf" class="on">360 flip</button><button id="b180">180 roll</button>
  <button id="br" class="on">right</button><button id="bl">left</button>
  <button id="bp">&#9654; replay</button>
</div>
<canvas id="graph" width="330" height="120"></canvas>
<div class="hint">drag to orbit &middot; scroll to zoom</div>

<script type="importmap">
{"imports":{"three":"./vendor/three.module.js","three/addons/":"./vendor/"}}
</script>
<script type="module">
import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';

const DATA = __DATA__;

// ---------- scene ----------
const app = document.getElementById('app');
const renderer = new THREE.WebGLRenderer({antialias:true});
renderer.setPixelRatio(Math.min(devicePixelRatio,2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
app.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0d0f13);
scene.fog = new THREE.Fog(0x0d0f13, 14, 34);

const camera = new THREE.PerspectiveCamera(42, innerWidth/innerHeight, 0.1, 200);
camera.position.set(6.2, 4.2, 6.8);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 0.9, 0);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.minDistance = 3;
controls.maxDistance = 22;
controls.maxPolarAngle = Math.PI * 0.495;

scene.add(new THREE.HemisphereLight(0xbfd4ff, 0x1a1f27, 1.15));
const key = new THREE.DirectionalLight(0xffffff, 2.1);
key.position.set(5, 9, 4);
key.castShadow = true;
key.shadow.mapSize.set(1024,1024);
key.shadow.camera.near = 1; key.shadow.camera.far = 30;
key.shadow.camera.left = -8; key.shadow.camera.right = 8;
key.shadow.camera.top = 8; key.shadow.camera.bottom = -8;
scene.add(key);
const rim = new THREE.DirectionalLight(0x4d7cff, 0.8);
rim.position.set(-6, 3, -5);
scene.add(rim);

// ground
const ground = new THREE.Mesh(
  new THREE.CircleGeometry(16, 64),
  new THREE.MeshStandardMaterial({color:0x141821, roughness:1, metalness:0})
);
ground.rotation.x = -Math.PI/2;
ground.receiveShadow = true;
scene.add(ground);
const grid = new THREE.GridHelper(32, 32, 0x222836, 0x1a1f29);
grid.position.y = 0.002;
scene.add(grid);

// ---------- quad ----------
// local frame: +X = nose (roll axis), +Y = up, +Z = right
const quad = new THREE.Group();
quad.position.y = 0.95;
scene.add(quad);

const bodyMat  = new THREE.MeshStandardMaterial({color:0x2f6df6, roughness:0.45, metalness:0.25});
const armMat   = new THREE.MeshStandardMaterial({color:0x2a303c, roughness:0.7});
const motorMat = new THREE.MeshStandardMaterial({color:0x9aa4b4, roughness:0.35, metalness:0.7});

const body = new THREE.Mesh(new THREE.BoxGeometry(1.5, 0.26, 1.5), bodyMat);
body.castShadow = true;
quad.add(body);

const canopy = new THREE.Mesh(new THREE.SphereGeometry(0.42, 20, 12, 0, Math.PI*2, 0, Math.PI/2),
                              new THREE.MeshStandardMaterial({color:0x1b2230, roughness:0.3}));
canopy.position.y = 0.1; canopy.scale.set(1,0.55,1);
quad.add(canopy);

const ARM = 1.35;
const corners = [[ARM,ARM],[ARM,-ARM],[-ARM,ARM],[-ARM,-ARM]];  // [x,z]
const rotors = [];
for (const [x,z] of corners) {
  const arm = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.12, 1.15), armMat);
  arm.position.set(x*0.5, 0, z*0.5);
  arm.rotation.y = Math.atan2(x, z);
  quad.add(arm);

  const motor = new THREE.Mesh(new THREE.CylinderGeometry(0.17,0.19,0.16,16), motorMat);
  motor.position.set(x, 0.08, z);
  motor.castShadow = true;
  quad.add(motor);

  const hub = new THREE.Group();
  hub.position.set(x, 0.2, z);
  quad.add(hub);

  const disc = new THREE.Mesh(
    new THREE.CylinderGeometry(0.52, 0.52, 0.012, 32),
    new THREE.MeshStandardMaterial({color:0x49b265, transparent:true, opacity:0.35,
                                    roughness:0.6, side:THREE.DoubleSide})
  );
  disc.position.y = 0.01;
  hub.add(disc);

  for (let b = 0; b < 2; b++) {
    const blade = new THREE.Mesh(new THREE.BoxGeometry(0.98, 0.02, 0.16),
                                 new THREE.MeshStandardMaterial({color:0x6fe08c, roughness:0.4}));
    blade.rotation.y = b * Math.PI/2;
    hub.add(blade);
  }
  rotors.push({hub, side: z > 0 ? 'right' : 'left'});
}

// nose marker + orientation lights
const nose = new THREE.Mesh(new THREE.ConeGeometry(0.17, 0.42, 12),
                            new THREE.MeshStandardMaterial({color:0xe8c547, roughness:0.4}));
nose.rotation.z = -Math.PI/2;
nose.position.set(0.95, 0.02, 0);
quad.add(nose);

const ledR = new THREE.Mesh(new THREE.SphereGeometry(0.075,12,10),
             new THREE.MeshStandardMaterial({color:0xff3b30, emissive:0xff3b30, emissiveIntensity:1.6}));
ledR.position.set(-0.7, 0.02, 0.7);
quad.add(ledR);
const ledG = new THREE.Mesh(new THREE.SphereGeometry(0.075,12,10),
             new THREE.MeshStandardMaterial({color:0x30d158, emissive:0x30d158, emissiveIntensity:1.6}));
ledG.position.set(-0.7, 0.02, -0.7);
quad.add(ledG);

// roll axis guide
const axis = new THREE.Mesh(new THREE.CylinderGeometry(0.008,0.008,4.6,8),
                            new THREE.MeshBasicMaterial({color:0x2b3342}));
axis.rotation.z = -Math.PI/2;
quad.add(axis);

// ---------- playback ----------
let kind = 'flip', dirn = 'right', frame = 0, playing = true;
const sel = (id,on)=>document.getElementById(id).classList.toggle('on',on);
function series(){ return DATA[kind + '-' + dirn]; }

document.getElementById('bf').onclick = ()=>{kind='flip';frame=0;sel('bf',true);sel('b180',false)};
document.getElementById('b180').onclick = ()=>{kind='roll180';frame=0;sel('b180',true);sel('bf',false)};
document.getElementById('br').onclick = ()=>{dirn='right';frame=0;sel('br',true);sel('bl',false)};
document.getElementById('bl').onclick = ()=>{dirn='left';frame=0;sel('bl',true);sel('br',false)};
document.getElementById('bp').onclick = ()=>{frame=0;playing=true};

const g = document.getElementById('graph').getContext('2d');
function drawGraph(rows, i){
  const W=330,H=120; g.clearRect(0,0,W,H);
  g.strokeStyle='#232936'; g.beginPath(); g.moveTo(0,H/2); g.lineTo(W,H/2); g.stroke();
  const tmax=rows.length, rmax=Math.max(200,...rows.map(r=>Math.abs(r.rate)));
  const y=v=>H/2-(v/rmax)*(H/2-14);
  g.strokeStyle='#495366'; g.lineWidth=1; g.beginPath();
  rows.forEach((r,k)=>{const x=k/tmax*W; k?g.lineTo(x,y(r.cmd)):g.moveTo(x,y(r.cmd))});
  g.stroke();
  g.strokeStyle='#30d158'; g.lineWidth=1.7; g.beginPath();
  rows.forEach((r,k)=>{const x=k/tmax*W; k?g.lineTo(x,y(r.rate)):g.moveTo(x,y(r.rate))});
  g.stroke();
  g.fillStyle='#e7e9ee'; g.fillRect(i/tmax*W,6,1.5,H-12);
  g.fillStyle='#7d8492'; g.font='10.5px system-ui';
  g.fillText('rate  command / actual', 8, 13);
}

const $=id=>document.getElementById(id);
let last=performance.now(), spin=0;
function tick(now){
  const dt=Math.min((now-last)/1000,0.05); last=now;
  const S=series(), rows=S.rows;
  if (playing){ frame=(frame+2)%(rows.length+50); }
  const i=Math.min(frame,rows.length-1), r=rows[i];

  quad.rotation.x = THREE.MathUtils.degToRad(r.angle);

  // prop differential: a roll speeds one side and slows the other, which is
  // exactly what distinguishes it from a yaw spin (where diagonals differ)
  const bias = Math.max(-1, Math.min(1, r.rate/720));
  spin += dt * 55;
  rotors.forEach(rt => {
    const k = rt.side === 'right' ? 1 - 0.55*bias : 1 + 0.55*bias;
    rt.hub.rotation.y = spin * k;
    rt.hub.scale.setScalar(0.9 + 0.1*Math.abs(k));
  });

  $('v_t').textContent = r.t.toFixed(0)+' / '+rows[rows.length-1].t.toFixed(0)+' ms';
  $('v_cmd').textContent = r.cmd.toFixed(0)+' deg/s';
  $('v_rate').textContent = r.rate.toFixed(0)+' deg/s';
  $('v_angle').textContent = r.angle.toFixed(0)+' deg';
  $('v_thr').textContent = (r.thr*100).toFixed(0)+' %';
  $('v_final').textContent = S.final.toFixed(0)+' deg';

  drawGraph(rows,i);
  controls.update();
  renderer.render(scene,camera);
  requestAnimationFrame(tick);
}
addEventListener('resize',()=>{
  camera.aspect=innerWidth/innerHeight; camera.updateProjectionMatrix();
  renderer.setSize(innerWidth,innerHeight);
});
requestAnimationFrame(tick);
</script></body></html>
"""

if __name__ == "__main__":
    main()
