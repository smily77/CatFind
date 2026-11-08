// target.js — Tab „Zielmodell" (Sitzende Katze). Konzept: GesamtKonzeptCatFinder.md,
// Abschnitt „Zielmodell Sitzende Katze". Zeigt den VPS-Nachbau (sitmodel.py) des
// CatIdent-Moduls sitModel.ino: Sitzplätze mit Entscheidung, Katzenkorridor (gelernt
// aus den als Katze bewerteten Tracks, von Hand bearbeitbar), Sperrstellen, die echten
// targetDetected des CatIdent und zum Vergleich catDetected (Normalmodell) und
// Kot-Verdachte. Gehört zu index.html (colorFor/escapeHtml) und analysis.js (anaJson,
// fmtDT, ANA, anaScheduleFetch, setView).
// Serverseite: /sit /sit_cfg /corridor_learn /corridor_set, Gerät: /sitparams.csv
// /sit_corridor.csv.
"use strict";

const TGT = {
  data: null, days: 7, view: null, sel: null, edit: null, onlyFired: true, showFox: false,
  drag: null, moved: false, timer: null,
};
const TGT_PARAMS = [
  ["sit_s", "Sitzdauer bis zum Auslösen ausserhalb des Korridors (s)"],
  ["sit_corr_s", "… im Katzenkorridor (s, 0 = dort nie)"],
  ["sit_r_mm", "Radius eines Sitzplatzes (mm)"],
  ["sit_gap_s", "so lange darf das Radar die Katze verlieren (s)"],
  ["sit_sd_mm", "max. Streuung je Achse (mm)"],
  ["sit_sd_min_mm", "min. Streuung je Achse (mm) — gegen eingefrorene Radarziele"],
  ["sit_min_rate", "min. Meldungen/s"],
  ["sit_max_rate", "max. Meldungen/s (sitzende Katze meldet dünn)"],
  ["sit_tau_s", "Gedächtnis für Schwerpunkt/Streuung (s)"],
  ["arrive_mm", "Ankunft: vorher mindestens so weit weg (mm)"],
  ["arrive_max_mm", "… aber höchstens (mm)"],
  ["arrive_window_s", "… innerhalb von (s)"],
  ["leave_mm", "weiter weg = Tier gegangen (mm)"],
  ["track_period_s", "Nachführung höchstens alle (s)"],
  ["track_max_s", "Nachführung bis (s nach dem Auslösen)"],
  ["sit_hlk_only", "nur HLK-Radare (1/0)"],
  ["log_s", "Sitzplätze ab dieser Dauer anzeigen (s)"],
  ["sit_enabled", "Nachbau aktiv (1/0)"],
];
const TGT_COL = {fire:"#f43b3b", place:"#8a8f98", dev:"#e64ce6", det:"#e6a23c", inc:"#c98a3c",
                 corr:"#2ecc71", block:"#9aa", cat:"#5dade2", fox:"#e67e22"};

function tgtEl(id){ return document.getElementById(id); }

async function tgtLoad(){
  const t1 = Date.now()/1000, t0 = TGT.days > 0 ? t1 - TGT.days*86400 : 0;
  try{ TGT.data = await anaJson(`/sit?t0=${t0}&t1=${t1}`); }
  catch(e){ tgtEl("tgtInfo").textContent = "Laden fehlgeschlagen: " + e.message; return; }
  if(!TGT.edit) tgtRenderParams();
  tgtRenderInfo(); tgtRenderList(); tgtRenderDays(); tgtDraw();
}

function tgtTransform(cv){
  const W = cv.clientWidth, H = cv.clientHeight, v = TGT.view;
  return { W, H,
    X: wx => W/2 + (wx - v.cx) * v.s, Y: wy => H/2 - (wy - v.cy) * v.s,
    invX: px => v.cx + (px - W/2) / v.s, invY: py => v.cy - (py - H/2) / v.s };
}
function tgtFit(){
  const cv = tgtEl("tgtMap"); if(!cv) return;
  const W = cv.clientWidth || 600, H = cv.clientHeight || 500;
  // Rasen grob 0..10 m x -1..13 m
  TGT.view = {cx: 5000, cy: 6000, s: Math.min(W/11000, H/15000)};
}

function tgtCells(){ return TGT.edit ? TGT.edit : (TGT.data?.corridor?.cells || []); }

function tgtDraw(){
  const cv = tgtEl("tgtMap"); if(!cv || cv.offsetParent === null) return;
  if(!TGT.view) tgtFit();
  const dpr = window.devicePixelRatio || 1;
  cv.width = cv.clientWidth*dpr; cv.height = cv.clientHeight*dpr;
  const ctx = cv.getContext("2d"); ctx.setTransform(dpr,0,0,dpr,0,0);
  const T = tgtTransform(cv), {X, Y, W, H} = T;
  ctx.fillStyle = "#0c0c10"; ctx.fillRect(0,0,W,H);
  ctx.strokeStyle = "#1d1d24"; ctx.lineWidth = 1;
  for(let g=-2000; g<=14000; g+=1000){
    ctx.beginPath(); ctx.moveTo(X(g),0); ctx.lineTo(X(g),H); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0,Y(g)); ctx.lineTo(W,Y(g)); ctx.stroke();
  }
  const d = TGT.data; if(!d) return;
  const cor = d.corridor || {}, cm = cor.cell_mm || 500, cw = cm * TGT.view.s;
  // Dichte der Katzen-Tracks (Erfahrungsdaten), dann Korridor-Zellen
  for(const [ix, iy, v] of (cor.density || [])){
    ctx.fillStyle = `rgba(80,140,255,${Math.min(0.55, v*1.6)})`;
    ctx.fillRect(X(ix*cm), Y((iy+1)*cm), cw, cw);
  }
  ctx.strokeStyle = TGT_COL.corr; ctx.lineWidth = TGT.edit ? 2 : 1.5;
  ctx.fillStyle = "rgba(46,204,113,0.18)";
  for(const [ix, iy] of tgtCells()){
    ctx.fillRect(X(ix*cm), Y((iy+1)*cm), cw, cw);
    ctx.strokeRect(X(ix*cm)+0.5, Y((iy+1)*cm)+0.5, cw-1, cw-1);
  }
  // No-Shot-Karte
  for(const ring of (d.noshot || [])){
    ctx.strokeStyle = "#e33"; ctx.globalAlpha = 0.35; ctx.lineWidth = 1;
    ctx.beginPath(); ring.forEach(([x,y],i)=> i ? ctx.lineTo(X(x),Y(y)) : ctx.moveTo(X(x),Y(y)));
    ctx.closePath(); ctx.stroke(); ctx.globalAlpha = 1;
  }
  // Sperrstellen
  ctx.setLineDash([4,4]); ctx.strokeStyle = TGT_COL.block;
  for(const b of (d.blocks || [])){ ctx.beginPath(); ctx.arc(X(b.x), Y(b.y), b.r*TGT.view.s, 0, 7); ctx.stroke(); }
  ctx.setLineDash([]);
  // Kot-Verdachte
  for(const i of (d.incidents || [])){
    ctx.strokeStyle = TGT_COL.inc; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(X(i.x), Y(i.y), 10, 0, 7); ctx.stroke();
  }
  // von Hand bewertete Tracks des Zeitraums: Katze (blau), Fuchs (orange, zuschaltbar)
  for(const t of (d.tracks || [])){
    if(t.mark === "insect" && !TGT.showFox) continue;
    ctx.strokeStyle = t.mark === "cat" ? TGT_COL.cat : TGT_COL.fox;
    ctx.globalAlpha = 0.55; ctx.lineWidth = 1.2;
    ctx.beginPath(); t.pts.forEach(([x,y],i)=> i ? ctx.lineTo(X(x),Y(y)) : ctx.moveTo(X(x),Y(y)));
    ctx.stroke(); ctx.globalAlpha = 1;
  }
  // catDetected nur, wenn es zu einem als Katze bewerteten Track gehört; targetDetected (Gerät) als Raute
  for(const a of (d.acts || [])){
    if(a.kind === "det" && a.track){ ctx.fillStyle = TGT_COL.det;
      ctx.beginPath(); ctx.arc(X(a.wx), Y(a.wy), 3, 0, 7); ctx.fill(); }
    if(a.kind === "tgt"){ const x = X(a.wx), y = Y(a.wy); ctx.fillStyle = TGT_COL.dev;
      ctx.beginPath(); ctx.moveTo(x, y-8); ctx.lineTo(x+8, y); ctx.lineTo(x, y+8); ctx.lineTo(x-8, y); ctx.closePath(); ctx.fill(); }
  }
  // Sitzplätze des Nachbaus
  for(const e of (d.events || [])){
    if(TGT.onlyFired && !e.fired) continue;
    const hot = TGT.sel === e.id;
    ctx.lineWidth = hot ? 3 : 1.5;
    if(e.fired){ ctx.fillStyle = TGT_COL.fire; ctx.beginPath(); ctx.arc(X(e.x), Y(e.y), hot?8:6, 0, 7); ctx.fill(); }
    ctx.strokeStyle = hot ? "#ffd23b" : (e.fired ? "#fff" : TGT_COL.place);
    ctx.beginPath(); ctx.arc(X(e.x), Y(e.y), hot?8:6, 0, 7); ctx.stroke();
  }
  ctx.fillStyle = "#778"; ctx.font = "11px system-ui";
  ctx.fillText(TGT.edit ? "Korridor bearbeiten: Klick schaltet eine 0,5-m-Zelle um" : "1 m Raster · Zellen blau = Dichte aller Katzen-Tracks · grün = Korridor · Linien = bewertete Tracks im Zeitraum", 8, H-8);
}

function tgtRenderInfo(){
  const d = TGT.data, el = tgtEl("tgtInfo"); if(!d || !el) return;
  const cor = d.corridor || {};
  const fired = d.events.filter(e=>e.fired).length;
  const dev = d.acts.filter(a=>a.kind==="tgt").length;
  const both = d.events.filter(e=>e.fired && e.dev_match).length;
  const devOnly = d.acts.filter(a=>a.kind==="tgt" && !a.vps_match).length;
  const nCat = d.tracks.filter(t=>t.mark==="cat").length, nFox = d.tracks.filter(t=>t.mark==="insect").length;
  el.innerHTML = `Nachbau gerechnet bis ${d.done_t ? fmtDT(d.done_t) : "…"} · ${fired} Auslösungen (VPS) · ` +
    `${dev} echte targetDetected (CatIdent)<br>Abgleich: ${both} beide · ${fired-both} nur VPS · ${devOnly} nur Gerät` +
    `<br>Bewertete Tracks im Zeitraum: ${nCat} Katze · ${nFox} Fuchs<br>Korridor: ${cor.cells?.length||0} Zellen, ` +
    `${escapeHtml(cor.source||"")}${cor.n_tracks ? ` aus ${cor.n_tracks} Katzen-Tracks (Schwelle ${Math.round((cor.thr||0)*100)} %)` : ""}`;
}

function tgtRenderList(){
  const el = tgtEl("tgtList"), d = TGT.data; if(!el || !d) return;
  const evs = d.events.filter(e=>!TGT.onlyFired || e.fired).slice().reverse().slice(0, 300);
  el.innerHTML = evs.map(e=>`<div class="tgtRow${e.fired?' fired':''}${TGT.sel===e.id?' sel':''}" data-id="${e.id}">
      <b>${e.fired ? "🎯 hätte geschossen" : "– nicht"}</b>${e.fired ? (e.dev_match ? " · Gerät ✓" : " · Gerät ✗") : ""} · ${fmtDT(e.fired ? e.t_fire : e.t_first)} · #${e.sender} ·
      ${(e.x/1000).toFixed(1)} / ${(e.y/1000).toFixed(1)} m${e.in_corr?' · Korridor':''}
      <small>sitzt ${Math.round(e.sit_s)} s · Streuung ${e.sdx}/${e.sdy} mm · ${e.rate}/s · Lücke ${e.maxgap} s · Ankunft ${((e.arrive_mm||0)/1000).toFixed(1)} m${e.reason?' · <i>'+escapeHtml(e.reason)+'</i>':''}</small>
    </div>`).join("") || "<div class='hint'>— keine Sitzplätze im Zeitraum —</div>";
  el.querySelectorAll(".tgtRow").forEach(r=>{
    r.onclick = ()=>{ const id = +r.dataset.id; TGT.sel = TGT.sel===id ? null : id;
      const e = d.events.find(x=>x.id===id); if(e && TGT.sel){ TGT.view.cx = e.x; TGT.view.cy = e.y; }
      tgtRenderList(); tgtDraw(); };
    r.ondblclick = ()=>{ const e = d.events.find(x=>x.id===+r.dataset.id); if(!e) return;
      // im Analyse-Tab ansehen
      ANA.win = {t0: e.t_first - 40, t1: e.t_end + 40}; ANA.follow = false;
      setView("ana", document.querySelector('.tab[data-v="ana"]')); };
  });
}

function tgtDayKey(t){ const d = new Date(t*1000); return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,"0")}-${String(d.getDate()).padStart(2,"0")}`; }
function tgtRenderDays(){
  const el = tgtEl("tgtDays"), d = TGT.data; if(!el || !d) return;
  const days = {};
  const get = k => days[k] || (days[k] = {det:0, detCat:0, hit:0, fire:0, dev:0, both:0, inc:0});
  for(const e of d.events) if(e.fired && e.dev_match) get(tgtDayKey(e.t_fire)).both++;
  for(const a of d.acts){ if(a.kind==="det"){ get(tgtDayKey(a.t)).det++; if(a.track) get(tgtDayKey(a.t)).detCat++; } if(a.kind==="hit") get(tgtDayKey(a.t)).hit++; if(a.kind==="tgt") get(tgtDayKey(a.t)).dev++; }
  for(const e of d.events) if(e.fired) get(tgtDayKey(e.t_fire)).fire++;
  for(const i of d.incidents) get(tgtDayKey(i.t_sit0)).inc++;
  const keys = Object.keys(days).sort().reverse();
  el.innerHTML = `<table class="tgtTab"><tr><th>Tag</th><th title="catDetected des Normalmodells (CatIdent), davon zu einem als Katze bewerteten Track">Normal (Katze)</th><th title="Schüsse der Aktoren">Schüsse</th><th title="Auslösungen des VPS-Nachbaus">Ziel VPS</th><th title="targetDetected des CatIdent (Schattenbetrieb)">Ziel Gerät</th><th title="VPS-Auslösung mit passendem targetDetected (±15 s, ≤1,5 m)">beide</th><th title="Kot-Verdachte">Kot?</th></tr>` +
    keys.map(k=>{ const v = days[k]; return `<tr><td>${k.slice(5)}</td><td>${v.det} (${v.detCat})</td><td>${v.hit}</td><td>${v.fire}</td><td>${v.dev}</td><td>${v.both}</td><td>${v.inc}</td></tr>`; }).join("") + "</table>";
}

function tgtRenderParams(){
  const el = tgtEl("tgtParams"), d = TGT.data; if(!el || !d) return;
  el.innerHTML = TGT_PARAMS.map(([k, l])=>`<label title="${escapeHtml(k)} · Standard ${d.defaults[k]}">${escapeHtml(l)}</label>
    <input type="number" step="any" data-k="${k}" value="${d.params[k]}">`).join("");
  tgtEl("tgtBlocks").value = JSON.stringify(d.blocks || [], null, 0);
}

async function tgtSaveParams(){
  const params = {};
  tgtEl("tgtParams").querySelectorAll("input[data-k]").forEach(i=>{ if(i.value !== "") params[i.dataset.k] = +i.value; });
  let blocks;
  try{ blocks = JSON.parse(tgtEl("tgtBlocks").value || "[]"); }
  catch(e){ alert("Sperrstellen: kein gültiges JSON"); return; }
  await anaJson("/sit_cfg", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({params, blocks})});
  tgtEl("tgtInfo").textContent = "gespeichert — Nachbau rechnet neu …";
  setTimeout(tgtLoad, 4000);
}

async function tgtLearn(){
  const thr = +tgtEl("tgtThr").value / 100;
  if(!confirm(`Korridor neu aus den als Katze bewerteten Tracks lernen (Schwelle ${Math.round(thr*100)} %)? Von Hand bearbeitete Zellen gehen dabei verloren.`)) return;
  await anaJson("/corridor_learn", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({thr})});
  TGT.edit = null; tgtEditUi(); setTimeout(tgtLoad, 1500);
}

function tgtEditUi(){
  tgtEl("tgtEditBtn").style.display = TGT.edit ? "none" : "";
  tgtEl("tgtEditTools").style.display = TGT.edit ? "" : "none";
  tgtDraw();
}

function tgtBind(){
  const cv = tgtEl("tgtMap");
  cv.addEventListener("mousedown", e=>{ TGT.drag = {x:e.offsetX, y:e.offsetY}; TGT.moved = false; });
  cv.addEventListener("mousemove", e=>{
    if(!TGT.drag) return;
    const dx = e.offsetX - TGT.drag.x, dy = e.offsetY - TGT.drag.y;
    if(Math.abs(dx) + Math.abs(dy) > 2) TGT.moved = true;
    TGT.view.cx -= dx / TGT.view.s; TGT.view.cy += dy / TGT.view.s;
    TGT.drag = {x:e.offsetX, y:e.offsetY}; tgtDraw();
  });
  cv.addEventListener("mouseup", e=>{
    const click = TGT.drag && !TGT.moved; TGT.drag = null;
    if(!click) return;
    const T = tgtTransform(cv), wx = T.invX(e.offsetX), wy = T.invY(e.offsetY);
    if(TGT.edit){
      const cm = TGT.data?.corridor?.cell_mm || 500, ix = Math.floor(wx/cm), iy = Math.floor(wy/cm);
      const i = TGT.edit.findIndex(c=>c[0]===ix && c[1]===iy);
      if(i >= 0) TGT.edit.splice(i, 1); else TGT.edit.push([ix, iy]);
      tgtDraw(); return;
    }
    // nächster Sitzplatz anwählen
    let best = null, bd = 15;
    for(const ev of (TGT.data?.events || [])){
      if(TGT.onlyFired && !ev.fired) continue;
      const d = Math.hypot(T.X(ev.x) - e.offsetX, T.Y(ev.y) - e.offsetY);
      if(d < bd){ bd = d; best = ev; }
    }
    TGT.sel = best ? best.id : null; tgtRenderList(); tgtDraw();
    const row = document.querySelector(".tgtRow.sel"); if(row) row.scrollIntoView({block:"nearest"});
  });
  cv.addEventListener("wheel", e=>{
    e.preventDefault();
    const T = tgtTransform(cv), wx = T.invX(e.offsetX), wy = T.invY(e.offsetY);
    TGT.view.s *= e.deltaY > 0 ? 1/1.2 : 1.2;
    TGT.view.cx = wx - (e.offsetX - T.W/2) / TGT.view.s;
    TGT.view.cy = wy + (e.offsetY - T.H/2) / TGT.view.s;
    tgtDraw();
  }, {passive:false});
  cv.addEventListener("dblclick", ()=>{ tgtFit(); tgtDraw(); });
  tgtEl("tgtDays").addEventListener("click", ()=>{});
  tgtEl("tgtRange").onchange = e=>{ TGT.days = +e.target.value; tgtLoad(); };
  tgtEl("tgtOnlyFired").onchange = e=>{ TGT.onlyFired = e.target.checked; tgtRenderList(); tgtDraw(); };
  tgtEl("tgtShowFox").onchange = e=>{ TGT.showFox = e.target.checked; tgtDraw(); };
  tgtEl("tgtReload").onclick = tgtLoad;
  tgtEl("tgtParamSave").onclick = tgtSaveParams;
  tgtEl("tgtParamDef").onclick = async ()=>{
    if(!confirm("Alle Zielmodell-Parameter auf Standard zurücksetzen?")) return;
    await anaJson("/sit_cfg", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({reset:true})});
    TGT.data = null; setTimeout(tgtLoad, 3000);
  };
  tgtEl("tgtLearn").onclick = tgtLearn;
  tgtEl("tgtEditBtn").onclick = ()=>{ TGT.edit = (TGT.data?.corridor?.cells || []).map(c=>[c[0], c[1]]); tgtEditUi(); };
  tgtEl("tgtEditSave").onclick = async ()=>{
    await anaJson("/corridor_set", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({cells: TGT.edit})});
    TGT.edit = null; tgtEditUi(); setTimeout(tgtLoad, 1500);
  };
  tgtEl("tgtEditDiscard").onclick = ()=>{ TGT.edit = null; tgtEditUi(); };
  window.addEventListener("keydown", e=>{ if(e.key === "Escape" && TGT.edit && view === "tgt"){ TGT.edit = null; tgtEditUi(); } });
  window.addEventListener("resize", ()=>{ if(view === "tgt") tgtDraw(); });
}

let tgtInit = false;
function tgtShow(){
  if(!tgtInit){ tgtInit = true; tgtBind(); }
  tgtLoad();
  clearInterval(TGT.timer);
  TGT.timer = setInterval(()=>{ if(view === "tgt" && !TGT.edit) tgtLoad(); }, 30000);
}
