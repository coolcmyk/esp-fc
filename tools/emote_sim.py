#!/usr/bin/env python3
"""Simulate the orion-fc flip emote and write an animated HTML view.

The profile constants are parsed from the firmware header, so the simulation
cannot drift from what the FC actually commands.
"""
import json
import pathlib
import re

HEADER = pathlib.Path(__file__).resolve().parents[1] / "lib/Espfc/src/Control/Emote.hpp"
OUT = pathlib.Path(__file__).resolve().parents[1] / "build" / "flip.html"

TAU = 0.045      # s, first-order rate response of the airframe
DT = 0.001       # s

def read_const(name, cast=float):
    m = re.search(rf"{name}\s*=\s*([0-9.]+)f?;", HEADER.read_text())
    if not m:
        raise SystemExit(f"constant {name} not found in {HEADER}")
    return cast(float(m.group(1)))

RAMP = read_const("RAMP_MS", int)
HOLD = read_const("FLIP_HOLD_MS", int)
DECEL = read_const("DECEL_MS", int)
BLIP = read_const("BLIP_MS", int)
PEAK = read_const("FLIP_PEAK_DPS")
THRUST = read_const("THRUST_SCALE")
BLIP_F = read_const("BLIP_FACTOR")
HOLD_180 = read_const("ROLL180_HOLD_MS", int)
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
    return rate * direction, (THRUST if t_ms < total else 1.0)


def simulate(kind, tau=TAU, direction=1):
    total = TOTAL_180 if kind == "roll180" else TOTAL
    rate, angle = 0.0, 0.0
    rows = []
    steps = int(total / (DT * 1000))
    for i in range(steps):
        t_ms = i * DT * 1000
        cmd, thr = profile(kind, t_ms, direction)
        rate += (cmd - rate) * (DT / tau)        # first-order lag
        angle += rate * DT
        rows.append({"t": round(t_ms, 1), "cmd": round(cmd, 2),
                     "rate": round(rate, 2), "angle": round(angle, 2),
                     "thr": round(thr, 3)})
    return rows, angle


def main():
    data = {}
    for kind in ("flip", "roll180"):
        for direction, label in ((1, "right"), (-1, "left")):
            rows, final = simulate(kind, direction=direction)
            data[f"{kind}-{label}"] = {"rows": rows, "final": round(final, 1)}
            print(f"{kind:8s} {label:5s}: {len(rows)} ms, final angle {final:7.1f} deg")

    html = HTML.replace("__DATA__", json.dumps(data))
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(html)
    print(f"wrote {OUT}")


HTML = """<!doctype html>
<html><head><meta charset="utf-8"><title>orion-fc flip emote</title>
<style>
 body{background:#111;color:#eee;font:14px/1.4 system-ui,sans-serif;margin:0;padding:24px}
 h1{font-size:17px;font-weight:600;margin:0 0 4px}
 .muted{color:#888;margin-bottom:18px}
 .wrap{display:flex;gap:28px;flex-wrap:wrap;align-items:flex-start}
 canvas{background:#181818;border-radius:10px}
 .panel{min-width:260px}
 .row{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #222}
 .k{color:#888}
 .v{font-variant-numeric:tabular-nums}
 button{background:#2a2a2a;color:#eee;border:1px solid #3a3a3a;border-radius:7px;
        padding:7px 13px;margin:0 6px 8px 0;cursor:pointer;font:inherit}
 button:hover{background:#333}
 button.on{background:#2d5bd7;border-color:#2d5bd7}
</style></head><body>
<h1>orion-fc &mdash; 360 flip emote</h1>
<div class="muted">profile from Emote.hpp &middot; first-order airframe lag &tau; = 45 ms</div>
<div>
 <button id="bf" class="on">360 flip</button><button id="b180">180 roll</button>
 <button id="br" class="on">right</button><button id="bl">left</button>
 <button id="bp">&#9654; replay</button>
</div>
<div class="wrap">
 <canvas id="quad" width="360" height="360"></canvas>
 <div class="panel">
  <div class="row"><span class="k">time</span><span class="v" id="v_t"></span></div>
  <div class="row"><span class="k">commanded rate</span><span class="v" id="v_cmd"></span></div>
  <div class="row"><span class="k">actual rate</span><span class="v" id="v_rate"></span></div>
  <div class="row"><span class="k">angle</span><span class="v" id="v_angle"></span></div>
  <div class="row"><span class="k">thrust</span><span class="v" id="v_thr"></span></div>
  <div class="row"><span class="k">final angle</span><span class="v" id="v_final"></span></div>
  <canvas id="plot" width="360" height="150" style="margin-top:14px"></canvas>
 </div>
</div>
<script>
const DATA = __DATA__;
let kind = "flip", dirn = "right", frame = 0, playing = true;

function series(){ return DATA[kind + "-" + dirn]; }

function drawQuad(angleDeg){
  const c = document.getElementById("quad"), g = c.getContext("2d");
  const cx = 180, cy = 180, R = 120;
  g.clearRect(0,0,360,360);
  const a = angleDeg * Math.PI / 180;
  g.save(); g.translate(cx, cy); g.rotate(a);
  // body
  g.fillStyle = "#2d5bd7"; g.fillRect(-26,-26,52,52);
  // arms + rotors
  g.strokeStyle = "#555"; g.lineWidth = 7;
  g.beginPath();
  g.moveTo(-26,-26); g.lineTo(-R,-R); g.moveTo(26,-26); g.lineTo(R,-R);
  g.moveTo(-26,26); g.lineTo(-R,R); g.moveTo(26,26); g.lineTo(R,R);
  g.stroke();
  g.fillStyle = "#3fa34d";
  [[-R,-R],[R,-R],[-R,R],[R,R]].forEach(([x,y]) => {
    g.beginPath(); g.arc(x,y,26,0,7); g.fill();
  });
  // direction marker (nose)
  g.fillStyle = "#e8c547";
  g.beginPath(); g.moveTo(0,-34); g.lineTo(-11,-18); g.lineTo(11,-18); g.fill();
  g.restore();
  // horizon reference
  g.strokeStyle = "#2a2a2a"; g.lineWidth = 2;
  g.beginPath(); g.moveTo(0,cy); g.lineTo(360,cy); g.stroke();
}

function drawPlot(rows, i){
  const c = document.getElementById("plot"), g = c.getContext("2d");
  g.clearRect(0,0,360,150);
  const tmax = rows.length;
  const rmax = Math.max(200, ...rows.map(r => Math.abs(r.rate)));
  const y = v => 75 - (v / rmax) * 60;
  g.strokeStyle = "#2a2a2a"; g.beginPath(); g.moveTo(0,75); g.lineTo(360,75); g.stroke();
  g.strokeStyle = "#555"; g.lineWidth = 1; g.beginPath();
  rows.forEach((r, k) => {
    const x = k / tmax * 360;
    k ? g.lineTo(x, y(r.cmd)) : g.moveTo(x, y(r.cmd));
  });
  g.stroke();
  g.strokeStyle = "#3fa34d"; g.lineWidth = 1.6; g.beginPath();
  rows.forEach((r, k) => {
    const x = k / tmax * 360;
    k ? g.lineTo(x, y(r.rate)) : g.moveTo(x, y(r.rate));
  });
  g.stroke();
  g.fillStyle = "#fff"; g.fillRect(i / tmax * 360, 0, 1.5, 150);
  g.fillStyle = "#888"; g.font = "11px system-ui";
  g.fillText("rate: command (grey) / actual (green)", 4, 12);
}

function tick(){
  const S = series(), rows = S.rows;
  if (playing){
    frame = (frame + 2) % (rows.length + 40);
  }
  const i = Math.min(frame, rows.length - 1);
  const r = rows[i];
  document.getElementById("v_t").textContent = r.t.toFixed(0) + " ms / " + rows[rows.length-1].t.toFixed(0) + " ms";
  document.getElementById("v_cmd").textContent = r.cmd.toFixed(0) + " deg/s";
  document.getElementById("v_rate").textContent = r.rate.toFixed(0) + " deg/s";
  document.getElementById("v_angle").textContent = r.angle.toFixed(0) + " deg";
  document.getElementById("v_thr").textContent = (r.thr * 100).toFixed(0) + " %";
  document.getElementById("v_final").textContent = S.final.toFixed(0) + " deg";
  drawQuad(r.angle);
  drawPlot(rows, i);
  requestAnimationFrame(tick);
}

function sel(id, on){ document.getElementById(id).classList.toggle("on", on); }
document.getElementById("bf").onclick = () => { kind="flip"; frame=0; sel("bf",true); sel("b180",false); };
document.getElementById("b180").onclick = () => { kind="roll180"; frame=0; sel("b180",true); sel("bf",false); };
document.getElementById("br").onclick = () => { dirn="right"; frame=0; sel("br",true); sel("bl",false); };
document.getElementById("bl").onclick = () => { dirn="left"; frame=0; sel("bl",true); sel("br",false); };
document.getElementById("bp").onclick = () => { frame=0; playing=true; };
requestAnimationFrame(tick);
</script></body></html>
"""

if __name__ == "__main__":
    main()
