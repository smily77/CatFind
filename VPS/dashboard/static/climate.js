// climate.js — Tab „Klima": Temperatur- und Luftdruckverlauf aus envReport
// (heute Dome-C1 #21, BMP180). Periode + Ende frei waehlbar, Mittelung je Schritt
// serverseitig (/env?hours=&t1=&step= liefert Mittel + Min/Max je Schritt).
// Ende = „jetzt" laedt jede Minute nach. Ziehen im Diagramm zoomt, Doppelklick
// geht einen Zoom zurueck. Gehoert zu index.html (setView ruft climShow/climHide).
"use strict";

const CLIM = {
  hours: 24,
  end: null,          // null = jetzt (laufend), sonst Unix-Sekunden
  data: null,         // /env-Antwort
  timer: null,
  zoom: [],           // Stapel {hours,end} fuer Doppelklick
  hoverT: null,
  drag: null,         // {x0,x1,canvas}
  busy: false,
  geo: {},            // je Canvas-id: {L,R,T,B,t0,t1,...}
};
const CLIM_COL = ["#ffb347", "#5ec8ff", "#9be36b", "#e87bd8"];
const CLIM_STEPS = [60, 300, 900, 1800, 3600, 10800, 21600, 43200, 86400];
const CLIM_GAP_S = 600;   // Messabstand ~1/min; groessere Luecke = Linie unterbrechen

function climEl(id){ return document.getElementById(id); }
function climPad(n){ return String(n).padStart(2, "0"); }
function climFmt(t, withDate){
  const d = new Date(t * 1000);
  const hm = `${climPad(d.getHours())}:${climPad(d.getMinutes())}`;
  return withDate ? `${climPad(d.getDate())}.${climPad(d.getMonth()+1)}.${String(d.getFullYear()).slice(2)} ${hm}` : hm;
}
function climToLocalInput(t){
  const d = new Date(t * 1000);
  return `${d.getFullYear()}-${climPad(d.getMonth()+1)}-${climPad(d.getDate())}T${climPad(d.getHours())}:${climPad(d.getMinutes())}`;
}
function climNum(v, k){ return v == null ? "–" : v.toFixed(k).replace(".", ","); }

function climStep(){
  const sel = climEl("climStep").value;
  if(sel !== "auto") return +sel;
  const span = CLIM.hours * 3600;
  if(span <= 12 * 3600) return 0;                       // Rohwerte (~1/min) reichen
  const want = span / 700;
  return CLIM_STEPS.find(s => s >= want) || 86400;
}

function climSyncInputs(){
  const per = climEl("climPer");
  let opt = [...per.options].find(o => +o.value === CLIM.hours);
  if(!opt){
    opt = per.querySelector("option[data-custom]") || per.appendChild(Object.assign(document.createElement("option"), {}));
    opt.dataset.custom = "1";
    opt.value = String(CLIM.hours);
    opt.textContent = CLIM.hours >= 48 ? `${climNum(CLIM.hours/24, 1)} Tage (Zoom)` : `${climNum(CLIM.hours, 1)} h (Zoom)`;
  }
  per.value = opt.value;
  climEl("climEnd").value = climToLocalInput(CLIM.end == null ? Date.now()/1000 : CLIM.end);
  climEl("climNow").classList.toggle("active", CLIM.end == null);
}

async function climLoad(){
  if(CLIM.busy) return;
  CLIM.busy = true;
  const step = climStep();
  const t1 = CLIM.end == null ? Date.now()/1000 : CLIM.end;
  climEl("climInfo").textContent = "lädt …";
  try{
    const r = await fetch(`env?hours=${CLIM.hours}&t1=${Math.round(t1)}&step=${step}`);
    CLIM.data = await r.json();
    const n = Object.values(CLIM.data.env || {}).reduce((a, o) => a + o.t.length, 0);
    climEl("climInfo").textContent = `${n} Punkte · ` +
      (step ? `Mittel je ${step >= 3600 ? climNum(step/3600, 0) + " h" : climNum(step/60, 0) + " min"}` : "Rohwerte") +
      (CLIM.end == null ? ` · Stand ${climFmt(Date.now()/1000)}` : "");
  }catch(e){
    climEl("climInfo").textContent = "Laden fehlgeschlagen: " + e;
  }finally{
    CLIM.busy = false;
  }
  climSyncInputs();
  climRender();
}

// ---- Zeichnen -----------------------------------------------------------------
function climNiceTicks(lo, hi, n){
  if(!(hi > lo)){ lo -= 0.5; hi += 0.5; }
  const raw = (hi - lo) / n, mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const st = [1, 2, 2.5, 5, 10].map(m => m * mag).find(s => s >= raw);
  const out = [];
  for(let v = Math.ceil(lo / st) * st; v <= hi + 1e-9; v += st) out.push(+v.toFixed(6));
  return {ticks: out, step: st};
}
function climTimeTicks(t0, t1, px){
  const span = t1 - t0, want = span / Math.max(2, px / 90);
  const opts = [600, 1800, 3600, 7200, 10800, 21600, 43200, 86400, 172800, 604800, 2592000];
  const st = opts.find(s => s >= want) || 2592000;
  const out = [];
  // an lokaler Mitternacht ausrichten
  const d = new Date(t0 * 1000); d.setHours(0, 0, 0, 0);
  let t = d.getTime() / 1000;
  if(st >= 2592000){       // Monatsanfaenge
    const m = new Date(t0 * 1000); m.setDate(1); m.setHours(0, 0, 0, 0);
    for(let k = 0; k < 40; k++){
      const x = new Date(m.getFullYear(), m.getMonth() + k, 1).getTime() / 1000;
      if(x > t1) break; if(x >= t0) out.push(x);
    }
    return {ticks: out, step: st};
  }
  while(t < t0) t += st;
  for(; t <= t1; t += st) out.push(t);
  return {ticks: out, step: st};
}

function climDrawChart(cid, key, kmin, kmax, unit, dec){
  const cv = climEl(cid), dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  if(!W || !H) return;
  cv.width = W * dpr; cv.height = H * dpr;
  const g = cv.getContext("2d");
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, W, H);
  const L = 56, R = W - 12, T = 22, B = H - 26;
  const D = CLIM.data, env = (D && D.env) || {};
  const t0 = D ? D.t0 : Date.now()/1000 - CLIM.hours*3600, t1 = D ? D.t1 : Date.now()/1000;
  const band = climEl("climBand").checked;
  let lo = Infinity, hi = -Infinity;
  for(const o of Object.values(env)){
    const a = band && o[kmin] ? o[kmin] : o[key], b = band && o[kmax] ? o[kmax] : o[key];
    for(const v of a) if(v < lo) lo = v;
    for(const v of b) if(v > hi) hi = v;
  }
  const title = key === "temp" ? "Temperatur" : "Luftdruck";
  g.font = "12px system-ui, sans-serif"; g.fillStyle = "#99a"; g.textAlign = "left";
  g.fillText(`${title} (${unit})`, L, 14);
  if(!isFinite(lo)){
    g.fillStyle = "#666"; g.textAlign = "center";
    g.fillText("keine Messwerte in diesem Zeitraum", (L + R) / 2, (T + B) / 2);
    CLIM.geo[cid] = null; return;
  }
  const padv = Math.max((hi - lo) * 0.08, key === "temp" ? 0.3 : 0.3);
  lo -= padv; hi += padv;
  const yt = climNiceTicks(lo, hi, 5);
  const X = t => L + (t - t0) / (t1 - t0) * (R - L), Y = v => B - (v - lo) / (hi - lo) * (B - T);
  CLIM.geo[cid] = {L, R, T, B, t0, t1, X, Y};

  // Raster
  g.strokeStyle = "#24242c"; g.lineWidth = 1; g.fillStyle = "#778"; g.font = "11px ui-monospace, monospace";
  g.textAlign = "right";
  for(const v of yt.ticks){
    const y = Math.round(Y(v)) + 0.5;
    g.beginPath(); g.moveTo(L, y); g.lineTo(R, y); g.stroke();
    g.fillText(v.toFixed(yt.step < 1 ? (yt.step < 0.1 ? 2 : 1) : 0).replace(".", ","), L - 6, y + 4);
  }
  const xt = climTimeTicks(t0, t1, R - L);
  g.textAlign = "center";
  for(const t of xt.ticks){
    const x = Math.round(X(t)) + 0.5, d = new Date(t * 1000);
    const midnight = d.getHours() === 0 && d.getMinutes() === 0;
    g.strokeStyle = midnight ? "#3a3a48" : "#24242c";
    g.beginPath(); g.moveTo(x, T); g.lineTo(x, B); g.stroke();
    const lab = xt.step >= 86400 ? `${climPad(d.getDate())}.${climPad(d.getMonth()+1)}.`
              : midnight ? `${climPad(d.getDate())}.${climPad(d.getMonth()+1)}.` : climFmt(t);
    g.fillStyle = midnight ? "#aab" : "#778";
    g.fillText(lab, x, B + 16);
  }

  // Reihen
  let ci = 0;
  for(const o of Object.values(env)){
    const col = CLIM_COL[ci++ % CLIM_COL.length], ts = o.t, vs = o[key];
    const gap = Math.max(CLIM_GAP_S, (D.step || 60) * 2.5);
    if(band && o[kmin] && o[kmin].length){
      g.fillStyle = col + "33";
      let i = 0;
      while(i < ts.length){
        let j = i; while(j + 1 < ts.length && ts[j + 1] - ts[j] <= gap) j++;
        g.beginPath();
        for(let k = i; k <= j; k++) g.lineTo(X(ts[k]), Y(o[kmax][k]));
        for(let k = j; k >= i; k--) g.lineTo(X(ts[k]), Y(o[kmin][k]));
        g.closePath(); g.fill();
        i = j + 1;
      }
    }
    g.strokeStyle = col; g.lineWidth = 1.8; g.lineJoin = "round";
    g.beginPath();
    for(let k = 0; k < ts.length; k++){
      if(k === 0 || ts[k] - ts[k - 1] > gap) g.moveTo(X(ts[k]), Y(vs[k]));
      else g.lineTo(X(ts[k]), Y(vs[k]));
    }
    g.stroke();
    if(ts.length === 1){ g.fillStyle = col; g.beginPath(); g.arc(X(ts[0]), Y(vs[0]), 3, 0, 7); g.fill(); }
    // Legende rechts oben
    g.fillStyle = col; g.textAlign = "right"; g.font = "12px system-ui, sans-serif";
    g.fillText(`${o.name}`, R - (ci - 1) * 110, 14);
  }

  // Auswahl beim Ziehen
  if(CLIM.drag && CLIM.drag.x1 != null){
    const a = Math.max(L, Math.min(CLIM.drag.x0, CLIM.drag.x1)), b = Math.min(R, Math.max(CLIM.drag.x0, CLIM.drag.x1));
    g.fillStyle = "#2d6cdf33"; g.fillRect(a, T, b - a, B - T);
  }
  // Fadenkreuz
  if(CLIM.hoverT != null && CLIM.hoverT >= t0 && CLIM.hoverT <= t1){
    const x = Math.round(X(CLIM.hoverT)) + 0.5;
    g.strokeStyle = "#889"; g.setLineDash([3, 3]);
    g.beginPath(); g.moveTo(x, T); g.lineTo(x, B); g.stroke(); g.setLineDash([]);
    ci = 0;
    for(const o of Object.values(env)){
      const k = climNearest(o.t, CLIM.hoverT); const col = CLIM_COL[ci++ % CLIM_COL.length];
      if(k < 0) continue;
      g.fillStyle = col; g.beginPath(); g.arc(X(o.t[k]), Y(o[key][k]), 3.5, 0, 7); g.fill();
    }
  }
}

function climNearest(ts, t){
  if(!ts.length) return -1;
  let lo = 0, hi = ts.length - 1;
  while(hi - lo > 1){ const m = (lo + hi) >> 1; if(ts[m] < t) lo = m; else hi = m; }
  return Math.abs(ts[lo] - t) <= Math.abs(ts[hi] - t) ? lo : hi;
}

function climStats(){
  const box = climEl("climStats"), env = (CLIM.data && CLIM.data.env) || {};
  const tiles = [];
  for(const o of Object.values(env)){
    if(!o.t.length) continue;
    const n = o.t.length, last = n - 1;
    const mean = a => a.reduce((s, v) => s + v, 0) / a.length;
    const argOf = (a, v) => a.indexOf(v);
    const tmin = o.tmin && o.tmin.length ? o.tmin : o.temp, tmax = o.tmax && o.tmax.length ? o.tmax : o.temp;
    const iMin = argOf(tmin, o.tempMin), iMax = argOf(tmax, o.tempMax);
    const dP = o.press[last] - o.press[0];
    const pre = Object.keys(env).length > 1 ? o.name + " · " : "";
    if(CLIM.end == null)
      tiles.push([`${climNum(o.temp[last], 1)} °C · ${climNum(o.press[last], 1)} hPa`, `${pre}${CLIM.data.step ? "letzter Schritt (Mittel)" : "aktuell"} (${climFmt(o.t[last])})`]);
    tiles.push([`${climNum(o.tempMin, 1)} … ${climNum(o.tempMax, 1)} °C`,
                `${pre}Temp. min ${iMin >= 0 ? climFmt(o.t[iMin], true) : ""} · max ${iMax >= 0 ? climFmt(o.t[iMax], true) : ""}`]);
    tiles.push([`${climNum(mean(o.temp), 1)} °C`, `${pre}Temp. Mittel`]);
    tiles.push([`${climNum(o.pressMin, 1)} … ${climNum(o.pressMax, 1)} hPa`, `${pre}Druck min/max`]);
    tiles.push([`${dP > 0 ? "+" : ""}${climNum(dP, 1)} hPa`, `${pre}Druck Änderung über die Periode`]);
    if(CLIM.end == null && (CLIM.data.step || 0) <= 900)
      tiles.push([`${o.pressTrend3h > 0 ? "+" : ""}${climNum(o.pressTrend3h, 1)} hPa`,
                  `${pre}Druck 3 h ${Math.abs(o.pressTrend3h) > 1 ? "(Wetterwechsel)" : "(ruhig)"}`]);
  }
  box.textContent = "";
  for(const [v, l] of tiles){
    const d = document.createElement("div"); d.className = "climStat";
    const b = document.createElement("b"); b.textContent = v;
    const s = document.createElement("span"); s.textContent = l;
    d.append(b, s); box.appendChild(d);
  }
}

function climRender(){
  climDrawChart("climT", "temp", "tmin", "tmax", "°C", 1);
  climDrawChart("climP", "press", "pmin", "pmax", "hPa", 1);
  climStats();
}

function climTip(ev){
  const tip = climEl("climTip"), env = (CLIM.data && CLIM.data.env) || {};
  if(CLIM.hoverT == null){ tip.style.display = "none"; return; }
  const lines = [];
  let tt = null;
  for(const o of Object.values(env)){
    const k = climNearest(o.t, CLIM.hoverT); if(k < 0) continue;
    tt = o.t[k];
    const pre = Object.keys(env).length > 1 ? o.name + ": " : "";
    let s = `${pre}${climNum(o.temp[k], 1)} °C · ${climNum(o.press[k], 1)} hPa`;
    if(o.tmin && o.tmin.length) s += `  (${climNum(o.tmin[k], 1)}…${climNum(o.tmax[k], 1)} °C)`;
    lines.push(s);
  }
  if(tt == null){ tip.style.display = "none"; return; }
  tip.textContent = climFmt(tt, true) + "\n" + lines.join("\n");
  tip.style.whiteSpace = "pre";
  const host = climEl("clim").getBoundingClientRect();
  tip.style.display = "block";
  const w = tip.offsetWidth;
  let x = ev.clientX - host.left + climEl("clim").scrollLeft + 14;
  if(x + w > host.width - 8) x = ev.clientX - host.left - w - 14;
  tip.style.left = x + "px";
  tip.style.top = (ev.clientY - host.top + climEl("clim").scrollTop + 12) + "px";
}

function climBindCanvas(cid){
  const cv = climEl(cid);
  const tAt = ev => {
    const gm = CLIM.geo[cid]; if(!gm) return null;
    const x = ev.clientX - cv.getBoundingClientRect().left;
    return gm.t0 + (Math.max(gm.L, Math.min(gm.R, x)) - gm.L) / (gm.R - gm.L) * (gm.t1 - gm.t0);
  };
  cv.addEventListener("mousemove", ev => {
    CLIM.hoverT = tAt(ev);
    if(CLIM.drag) CLIM.drag.x1 = ev.clientX - cv.getBoundingClientRect().left;
    climRender(); climTip(ev);
  });
  cv.addEventListener("mouseleave", () => { CLIM.hoverT = null; climEl("climTip").style.display = "none"; climRender(); });
  cv.addEventListener("mousedown", ev => {
    if(ev.button !== 0) return;
    CLIM.drag = {x0: ev.clientX - cv.getBoundingClientRect().left, x1: null, cid, t0: tAt(ev)};
  });
  cv.addEventListener("mouseup", ev => {
    const d = CLIM.drag; CLIM.drag = null;
    if(!d || d.x1 == null || Math.abs(d.x1 - d.x0) < 6){ climRender(); return; }
    const ta = d.t0, tb = tAt(ev);
    const a = Math.min(ta, tb), b = Math.max(ta, tb);
    if(b - a < 600){ climRender(); return; }          // mind. 10 min
    CLIM.zoom.push({hours: CLIM.hours, end: CLIM.end});
    CLIM.hours = +((b - a) / 3600).toFixed(3);
    CLIM.end = Math.round(b);
    climLoad();
  });
  cv.addEventListener("dblclick", () => {
    const z = CLIM.zoom.pop(); if(!z) return;
    CLIM.hours = z.hours; CLIM.end = z.end; climLoad();
  });
  // Touch: Tippen zeigt Werte
  cv.addEventListener("touchstart", ev => {
    const t = ev.touches[0]; CLIM.hoverT = tAt(t); climRender(); climTip(t);
  }, {passive: true});
}

function climShift(dir){
  const now = Date.now()/1000, cur = CLIM.end == null ? now : CLIM.end;
  let e = cur + dir * CLIM.hours * 3600;
  CLIM.end = e >= now - 60 ? null : Math.round(e);
  climLoad();
}

function climShow(){
  climSyncInputs();
  climLoad();
  clearInterval(CLIM.timer);
  CLIM.timer = setInterval(() => { if(CLIM.end == null && !document.hidden) climLoad(); }, 60000);
}
function climHide(){ clearInterval(CLIM.timer); CLIM.timer = null; }

(function climInit(){
  if(!climEl("clim")) return;
  climEl("climPer").addEventListener("change", e => {
    CLIM.hours = +e.target.value; CLIM.zoom = [];
    const c = e.target.querySelector("option[data-custom]");
    if(c && c.value !== e.target.value) c.remove();
    climLoad();
  });
  climEl("climEnd").addEventListener("change", e => {
    const t = new Date(e.target.value).getTime() / 1000;
    if(!isFinite(t)) return;
    CLIM.end = t >= Date.now()/1000 - 60 ? null : t; climLoad();
  });
  climEl("climPrev").addEventListener("click", () => climShift(-1));
  climEl("climNext").addEventListener("click", () => climShift(1));
  climEl("climNow").addEventListener("click", () => { CLIM.end = null; climLoad(); });
  climEl("climStep").addEventListener("change", climLoad);
  climEl("climBand").addEventListener("change", climRender);
  climBindCanvas("climT"); climBindCanvas("climP");
  window.addEventListener("resize", () => { if(climEl("clim").style.display !== "none") climRender(); });
  document.addEventListener("mouseup", () => { if(CLIM.drag){ CLIM.drag = null; climRender(); } });
})();
