// coverage.js — Coverage-Tab: Erfassungs-Polygone der Sensoren an einem Ort.
// Lernen aus einer Mäherfahrt, Eckpunkte von Hand verschieben (keine neuen
// Punkte), löschen (= zurück auf den xComDef-Sektor). Gehört zu index.html
// (nutzt colorFor/escapeHtml/minMaxArr/mapPoints/postCmd) und analysis.js
// (anaJson, fmtDT, distToSeg, ANA für Zeitfenster + Erfassungs-Overlay).
// Serverseite: /coverage_profiles /coverage_learn /coverage_set /coverage_delete
// /adata /noshot. Nach jeder Änderung lädt der CatIdentifier (#18) per
// "Parameter laden" (cmd 22) die Polygone neu (/coverage_export.csv).
"use strict";

const COV = {
  data: null,           // /coverage_profiles (+ current/sources/names)
  sid: null,            // gewählter Sensor
  win: null,            // {t0,t1} Zeitfenster für Treffer + Lernen
  events: [],           // /adata des Fensters
  noshot: null,
  preview: null,        // gelerntes Polygon, noch nicht gespeichert
  edit: null,           // Arbeitskopie der Ecken beim Verschieben
  editSel: -1,
  view: null,           // {cx,cy,s} Welt-mm -> px
};
const COV_CATIDENT = 18, COV_CMD_LOAD = 22;
const COV_PT_PX = 10;

function covEl(id){ return document.getElementById(id); }
function covMsg(s, err){ const el = covEl("covMsg"); el.textContent = s; el.style.color = err ? "#e66" : "#9ab"; }
function covName(sid){ return (COV.data?.names?.[sid] || "#"+sid) + " #" + sid; }

// datetime-local <-> Unix-Sekunden (Ortszeit des Browsers)
function covToInput(t){
  const d = new Date(t*1000), p = n => String(n).padStart(2,"0");
  return `${d.getFullYear()}-${p(d.getMonth()+1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}`;
}
function covFromInput(s){ const t = new Date(s).getTime()/1000; return isFinite(t) ? t : null; }
function covSetWin(t0, t1){
  COV.win = {t0, t1};
  covEl("covT0").value = covToInput(t0);
  covEl("covT1").value = covToInput(t1);
}

async function covLoad(){
  COV.data = await anaJson("/coverage_profiles");
  const ids = Object.keys(COV.data.current).sort((a,b)=>a-b);
  if(COV.sid == null || !ids.includes(COV.sid)) COV.sid = ids.find(s=>COV.data.sources[s]==="learned") || ids[0] || null;
  covRenderList();
  covDraw();
}

async function covLoadEvents(){
  if(!COV.win) return;
  try{
    const r = await anaJson(`/adata?t0=${COV.win.t0}&t1=${COV.win.t1}`);
    COV.events = r.events.filter(e=>e.wv===1);
    covEl("covEvInfo").textContent = `${COV.events.length} Treffer im Fenster` +
      (r.stride > 1 ? ` (jeder ${r.stride}.)` : "");
  }catch(e){ COV.events = []; covEl("covEvInfo").textContent = "Fehler: " + e.message; }
  covDraw();
}

function covRenderList(){
  const d = COV.data, el = covEl("covList");
  const ids = Object.keys(d.current).sort((a,b)=>a-b);
  // deaktivierte Profile (Pose geändert / gelöscht) zur Info mit anzeigen
  const off = Object.values(d.profiles).filter(p=>!(p.active && p.pose_matches));
  el.innerHTML = ids.map(sid=>{
    const src = d.sources[sid], pr = d.profiles[sid];
    const poly = d.current[sid];
    const what = src === "learned" ? (pr?.note || "gelernt") : "Default-Sektor (xComDef)";
    const area = (catArea(poly)/1e6).toFixed(1);
    return `<div class="covItem${sid===COV.sid?' sel':''}" data-sid="${sid}">
      <i style="background:${colorFor(+sid,0)}"></i><b>${escapeHtml(covName(sid))}</b>
      <small>${escapeHtml(what)} · ${poly.length} Ecken · ${area} m²` +
      (src==="learned" && pr?.created ? ` · ${fmtDT(pr.created)}` : "") + `</small></div>`;
  }).join("") || `<div class="hint">— keine Sensoren mit gültiger Pose —</div>`;
  el.innerHTML += off.map(p=>`<div class="covItem off"><b>${escapeHtml(covName(p.sender))}</b>
      <small>gelerntes Polygon inaktiv: ${escapeHtml(p.note || "")}</small></div>`).join("");
  el.querySelectorAll(".covItem[data-sid]").forEach(it=>{
    it.onclick = ()=>{
      if(COV.edit && !confirm("Bearbeitung verwerfen?")) return;
      COV.sid = it.dataset.sid; COV.edit = null; COV.preview = null;
      covRenderList(); covUpdateButtons(); covDraw();
    };
  });
  covUpdateButtons();
}

function catArea(poly){
  let a = 0;
  for(let i=0;i<poly.length;i++){ const [x1,y1]=poly[i], [x2,y2]=poly[(i+1)%poly.length]; a += x1*y2 - x2*y1; }
  return Math.abs(a)/2;
}

function covUpdateButtons(){
  const editing = !!COV.edit, has = COV.sid != null;
  const learned = has && COV.data?.sources?.[COV.sid] === "learned";
  covEl("covSel").textContent = has ? covName(COV.sid) : "—";
  covEl("covEditBtn").style.display = editing ? "none" : "";
  covEl("covEditTools").style.display = editing ? "" : "none";
  covEl("covEditBtn").disabled = !has;
  covEl("covLearnBtn").disabled = !has || editing;
  covEl("covDelBtn").disabled = !learned || editing;
  covEl("covPrevSave").style.display = COV.preview && !editing ? "" : "none";
  covEl("covPrevDrop").style.display = COV.preview && !editing ? "" : "none";
  covEl("covMap").classList.toggle("editing", editing);
}

// --------------------------------------------------------------- Aktionen

// Nach jeder Änderung: CatIdent holt Parameter + Coverage neu, Analyse-Overlay neu laden
async function covChanged(text){
  postCmd(COV_CATIDENT, COV_CMD_LOAD, 0);
  if(typeof ANA !== "undefined"){
    ANA.coverage = null;
    if(ANA.showCov){ try{ ANA.coverage = await anaJson("/acoverage"); }catch(e){} }
  }
  await covLoad();
  covMsg(text + " — CatIdentifier lädt neu.");
}

async function covLearn(){
  if(COV.sid == null || !COV.win) return;
  covMsg("lernt …");
  try{
    const r = await anaJson("/coverage_learn", {method:"POST", headers:{"Content-Type":"application/json"},
      body: JSON.stringify({sender:+COV.sid, t0:COV.win.t0, t1:COV.win.t1, angle_bin_deg:10,
                            quantile:0.90, target_vertices:8, save:false})});
    COV.preview = {sid: COV.sid, poly: r.polygon, t0: COV.win.t0, t1: COV.win.t1};
    covMsg(`Vorschau (gestrichelt): ${r.polygon.length} Ecken, ${r.point_count} Punkte, ` +
           `${((r.quality?.area_mm2||0)/1e6).toFixed(1)} m². Passt sie, „Vorschau speichern".`);
  }catch(e){ COV.preview = null; covMsg("Lernen fehlgeschlagen: " + e.message, true); }
  covUpdateButtons(); covDraw();
}

async function covPreviewSave(){
  const p = COV.preview; if(!p) return;
  try{
    await anaJson("/coverage_learn", {method:"POST", headers:{"Content-Type":"application/json"},
      body: JSON.stringify({sender:+p.sid, t0:p.t0, t1:p.t1, angle_bin_deg:10,
                            quantile:0.90, target_vertices:8, save:true})});
    COV.preview = null;
    await covChanged("Gelerntes Polygon gespeichert");
  }catch(e){ covMsg("Speichern fehlgeschlagen: " + e.message, true); }
}

async function covDelete(){
  if(COV.sid == null) return;
  if(!confirm(`Gelerntes Polygon von ${covName(COV.sid)} löschen? Danach gilt wieder der Default-Sektor.`)) return;
  try{
    await anaJson("/coverage_delete", {method:"POST", headers:{"Content-Type":"application/json"},
      body: JSON.stringify({sender:+COV.sid})});
    await covChanged("Polygon gelöscht, Default-Sektor aktiv");
  }catch(e){ covMsg("Löschen fehlgeschlagen: " + e.message, true); }
}

function covEditStart(){
  const poly = COV.data?.current?.[COV.sid]; if(!poly) return;
  COV.preview = null;
  COV.edit = poly.map(([x,y])=>[x,y]);
  COV.editSel = -1;
  COV.editDirty = false;
  covMsg("Ecken ziehen. Neue Punkte gibt es bewusst nicht. Esc = verwerfen.");
  covUpdateButtons(); covDraw();
}
function covEditStop(){ COV.edit = null; COV.editSel = -1; covUpdateButtons(); covDraw(); }
function covEditDiscard(){
  if(!COV.edit) return;
  if(COV.editDirty && !confirm("Änderungen verwerfen?")) return;
  COV.editDirty = false; covEditStop(); covMsg("verworfen");
}
async function covEditSave(){
  if(!COV.edit) return;
  try{
    await anaJson("/coverage_set", {method:"POST", headers:{"Content-Type":"application/json"},
      body: JSON.stringify({sender:+COV.sid, polygon:COV.edit.map(([x,y])=>[Math.round(x),Math.round(y)])})});
    COV.editDirty = false; covEditStop();
    await covChanged("Angepasstes Polygon gespeichert");
  }catch(e){ covMsg("Speichern fehlgeschlagen: " + e.message, true); }
}

// --------------------------------------------------------------- Karte

function covFit(){
  const cv = covEl("covMap"), W = cv.clientWidth, H = cv.clientHeight;
  const xs = [], ys = [];
  (mapPoints||[]).forEach(p=>{ xs.push(p[0]*1000); ys.push(p[1]*1000); });
  Object.values(COV.data?.current||{}).forEach(poly=>poly.forEach(([x,y])=>{ xs.push(x); ys.push(y); }));
  if(xs.length < 2){ COV.view = {cx:0, cy:5000, s:0.05}; return; }
  const [minx,maxx] = minMaxArr(xs), [miny,maxy] = minMaxArr(ys);
  COV.view = {cx:(minx+maxx)/2, cy:(miny+maxy)/2,
              s: Math.min(W/Math.max(maxx-minx,1000), H/Math.max(maxy-miny,1000)) * 0.9};
}
function covT(cv){
  const v = COV.view, W = cv.clientWidth, H = cv.clientHeight;
  return {W, H, X: x => W/2 + (x-v.cx)*v.s, Y: y => H/2 - (y-v.cy)*v.s,
          invX: px => v.cx + (px-W/2)/v.s, invY: py => v.cy - (py-H/2)/v.s};
}
function covPath(ctx, T, poly){
  ctx.beginPath();
  poly.forEach(([x,y],i)=> i ? ctx.lineTo(T.X(x),T.Y(y)) : ctx.moveTo(T.X(x),T.Y(y)));
  ctx.closePath();
}

function covDraw(){
  const cv = covEl("covMap"); if(!cv || cv.offsetParent === null || !COV.data) return;
  if(!COV.view) covFit();
  const dpr = window.devicePixelRatio || 1, W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W*dpr; cv.height = H*dpr;
  const ctx = cv.getContext("2d"); ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.fillStyle = "#0c0c10"; ctx.fillRect(0,0,W,H);
  const T = covT(cv), v = COV.view;
  // Gitter 1 m bzw. 5 m
  const step = v.s*1000 > 25 ? 1000 : 5000;
  ctx.strokeStyle = "#1d1d24"; ctx.lineWidth = 1;
  for(let gx=Math.ceil(T.invX(0)/step)*step; gx<=T.invX(W); gx+=step){ ctx.beginPath(); ctx.moveTo(T.X(gx),0); ctx.lineTo(T.X(gx),H); ctx.stroke(); }
  for(let gy=Math.ceil(T.invY(H)/step)*step; gy<=T.invY(0); gy+=step){ ctx.beginPath(); ctx.moveTo(0,T.Y(gy)); ctx.lineTo(W,T.Y(gy)); ctx.stroke(); }
  // RasenKarte + No-Shot
  ctx.fillStyle = "#556"; (mapPoints||[]).forEach(p=>ctx.fillRect(T.X(p[0]*1000)-1.5, T.Y(p[1]*1000)-1.5, 3, 3));
  for(const ring of (COV.noshot||[])){
    ctx.strokeStyle = "#e33"; ctx.globalAlpha = 0.35; ctx.lineWidth = 1; covPath(ctx, T, ring); ctx.stroke();
  }
  ctx.globalAlpha = 1;
  // übrige Sensoren dezent, gewählter kräftig
  const onlySel = covEl("covOnlySel").checked;
  for(const [sid, poly] of Object.entries(COV.data.current)){
    if(sid === COV.sid || onlySel) continue;
    const col = colorFor(+sid, 0);
    covPath(ctx, T, poly);
    ctx.fillStyle = col; ctx.globalAlpha = 0.05; ctx.fill();
    ctx.globalAlpha = 0.35; ctx.strokeStyle = col; ctx.lineWidth = 1; ctx.stroke();
    ctx.globalAlpha = 1;
  }
  // Treffer des Fensters (nur gewählter Sensor, ausser "alle Treffer")
  const allEv = covEl("covAllEv").checked;
  for(const e of COV.events){
    if(!allEv && String(e.sender) !== COV.sid) continue;
    ctx.fillStyle = colorFor(e.sender, e.sensor); ctx.globalAlpha = 0.75;
    ctx.beginPath(); ctx.arc(T.X(e.wx), T.Y(e.wy), 2.5, 0, 7); ctx.fill();
  }
  ctx.globalAlpha = 1;
  if(COV.sid != null){
    const col = colorFor(+COV.sid, 0);
    const cur = COV.data.current[COV.sid];
    const pose = COV.data.poses[COV.sid];
    if(cur){
      covPath(ctx, T, cur);
      ctx.fillStyle = col; ctx.globalAlpha = COV.edit ? 0.04 : 0.12; ctx.fill();
      ctx.globalAlpha = COV.edit ? 0.5 : 0.9; ctx.strokeStyle = col; ctx.lineWidth = 2;
      if(COV.edit) ctx.setLineDash([4,4]);
      ctx.stroke(); ctx.setLineDash([]); ctx.globalAlpha = 1;
    }
    if(COV.preview && COV.preview.sid === COV.sid){
      covPath(ctx, T, COV.preview.poly);
      ctx.strokeStyle = "#fff"; ctx.lineWidth = 2; ctx.setLineDash([7,5]); ctx.stroke(); ctx.setLineDash([]);
    }
    if(COV.edit){
      covPath(ctx, T, COV.edit);
      ctx.fillStyle = "#f5a623"; ctx.globalAlpha = 0.10; ctx.fill(); ctx.globalAlpha = 1;
      ctx.strokeStyle = "#f5a623"; ctx.lineWidth = 2; ctx.stroke();
      COV.edit.forEach(([x,y],i)=>{
        const sel = i === COV.editSel;
        ctx.fillStyle = sel ? "#ffd23b" : "#f5a623";
        ctx.beginPath(); ctx.arc(T.X(x), T.Y(y), sel?7:5.5, 0, 7); ctx.fill();
      });
      if(COV.editSel >= 0){
        const [x,y] = COV.edit[COV.editSel], [ox,oy] = (cur||[])[COV.editSel] || [x,y];
        const dd = Math.hypot(x-ox, y-oy);
        ctx.fillStyle = "#ffd23b"; ctx.font = "11px system-ui";
        ctx.fillText(`${(x/1000).toFixed(2)} / ${(y/1000).toFixed(2)} m` + (dd > 1 ? ` · verschoben ${(dd/1000).toFixed(2)} m` : ""),
                     T.X(x)+10, T.Y(y)-10);
      }
    }
    if(pose){
      ctx.fillStyle = col; ctx.beginPath(); ctx.arc(T.X(pose.x), T.Y(pose.y), 5, 0, 7); ctx.fill();
      ctx.strokeStyle = "#fff"; ctx.lineWidth = 1; ctx.stroke();
    }
  }
  ctx.fillStyle = "#778"; ctx.font = "11px system-ui";
  ctx.fillText((step/1000) + " m Raster", 8, H-8);
}

function covHitPoint(T, px, py){
  if(!COV.edit) return -1;
  let best = -1, bd = COV_PT_PX;
  COV.edit.forEach(([x,y],i)=>{ const d = Math.hypot(T.X(x)-px, T.Y(y)-py); if(d <= bd){ bd = d; best = i; } });
  return best;
}

function covBind(){
  const cv = covEl("covMap");
  let drag = null;
  const pos = e => { const r = cv.getBoundingClientRect(); return {x:e.clientX-r.left, y:e.clientY-r.top}; };
  cv.addEventListener("pointerdown", e=>{
    const p = pos(e); cv.setPointerCapture(e.pointerId);
    const hit = covHitPoint(covT(cv), p.x, p.y);
    if(hit >= 0){ COV.editSel = hit; drag = {pt:hit}; covDraw(); return; }
    drag = {pan:true, x:p.x, y:p.y};
  });
  cv.addEventListener("pointermove", e=>{
    if(!drag) return;
    const p = pos(e), T = covT(cv);
    if(drag.pt != null){
      COV.edit[drag.pt] = [T.invX(p.x), T.invY(p.y)];
      COV.editDirty = true;
    } else {
      COV.view.cx -= (p.x-drag.x)/COV.view.s; COV.view.cy += (p.y-drag.y)/COV.view.s;
      drag.x = p.x; drag.y = p.y;
    }
    covDraw();
  });
  cv.addEventListener("pointerup", ()=>{ drag = null; });
  cv.addEventListener("wheel", e=>{
    e.preventDefault();
    const p = pos(e), v = COV.view, T = covT(cv);
    const wx = T.invX(p.x), wy = T.invY(p.y);
    v.s *= e.deltaY > 0 ? 1/1.25 : 1.25;
    v.cx = wx - (p.x - cv.clientWidth/2)/v.s; v.cy = wy + (p.y - cv.clientHeight/2)/v.s;
    covDraw();
  }, {passive:false});
  cv.addEventListener("dblclick", ()=>{ if(!COV.edit){ covFit(); covDraw(); } });
  window.addEventListener("keydown", e=>{
    if(view !== "cov" || !COV.edit) return;
    if(e.key === "Escape"){ e.preventDefault(); covEditDiscard(); }
  });
  window.addEventListener("resize", ()=>{ if(view === "cov") covDraw(); });
}

// --------------------------------------------------------------- Einstieg

let covInit = false;
async function covShow(){
  if(!covInit){
    covInit = true;
    covBind();
    covEl("covEditBtn").onclick = covEditStart;
    covEl("covEditSave").onclick = covEditSave;
    covEl("covEditDiscard").onclick = covEditDiscard;
    covEl("covLearnBtn").onclick = covLearn;
    covEl("covPrevSave").onclick = covPreviewSave;
    covEl("covPrevDrop").onclick = ()=>{ COV.preview = null; covUpdateButtons(); covDraw(); covMsg(""); };
    covEl("covDelBtn").onclick = covDelete;
    covEl("covReload").onclick = ()=>covLoad().catch(e=>covMsg(e.message, true));
    covEl("covOnlySel").onchange = covDraw;
    covEl("covAllEv").onchange = covDraw;
    covEl("covEvLoad").onclick = ()=>{
      const t0 = covFromInput(covEl("covT0").value), t1 = covFromInput(covEl("covT1").value);
      if(t0 == null || t1 == null || t1 <= t0){ covMsg("Zeitfenster ungültig", true); return; }
      COV.win = {t0, t1}; covLoadEvents();
    };
    covEl("covFromAna").onclick = ()=>{
      const r = (typeof ANA !== "undefined") && (ANA.sel || ANA.win);
      if(!r){ covMsg("Im Analyse-Tab ist noch kein Zeitfenster gewählt", true); return; }
      covSetWin(r.t0, r.t1); covLoadEvents();
    };
    anaJson("/noshot").then(r=>{ COV.noshot = r.rings||[]; covDraw(); }).catch(()=>{});
    // Start-Fenster: Shift-Auswahl bzw. Fenster aus dem Analyse-Tab, sonst letzte Stunde
    const r = (typeof ANA !== "undefined") && (ANA.sel || ANA.win || anaLoadWin());
    const now = Date.now()/1000;
    covSetWin(r ? r.t0 : now-3600, r ? r.t1 : now);
  }
  try{ await covLoad(); }catch(e){ covMsg("Laden fehlgeschlagen: " + e.message, true); }
  covLoadEvents();
}
