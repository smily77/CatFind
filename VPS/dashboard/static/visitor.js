// visitor.js — Tab „Besucher" (Spin-off BurglerProtect, SpinOff/BurglerProtect/
// Dokumentation/Konzept.md). Zeigt die visitorObserved-Meldungen des Dome-C1
// (Veranda, Welt X>0/Y<0, ausserhalb seiner Filterkarte), das Auslösepolygon
// (zeichnen/verschieben/löschen) und die Regel-Parameter. Die Auswertung „hätte
// ausgelöst / hält das Licht an" rechnet der VPS mit derselben Regel wie später
// das Steuergerät (visitor.py). Gehört zu index.html (nutzt colorFor/escapeHtml/
// minMaxArr/mapPoints) und analysis.js (anaJson, fmtDT, distToSeg).
// Scharf/Aus und Meldungen des Steuergeraets Besucherlicht (#22) kommen ueber
// /state (settingsReport stgArmed, Debug-Zeilen "Besucherlicht: ...").
// Serverseite: /visitors /visitor_cfg /visitor_reset, Geraet: /visitor_device.cfg.
"use strict";

const VIS = {
  data: null,          // /visitors
  cfg: null,           // /visitor_cfg (poly, params, defaults, limits)
  hours: 24,
  view: null,          // {cx,cy,s} Welt-mm -> px
  edit: null,          // Arbeitskopie der Polygon-Ecken beim Zeichnen
  editSel: -1,
  drag: null,
  selT0: null,         // Beginn der in der Liste angeklickten Spur (Spur-Nr. wechselt beim Nachladen)
  timer: null,
  armTimer: null,
};
const VIS_DEV = 22, VIS_STG_ARMED = 13, VIS_CMD_RELOAD = 22;
const VIS_PT_PX = 10;
const VIS_PARAMS = [
  ["min_events", "Mindestanzahl Meldungen einer Spur im Polygon"],
  ["window_s",   "… innerhalb von (s)"],
  ["min_span_s", "… die mindestens so lange dauern (s)"],
  ["link_mm",    "Spur: grösster Sprung (mm)"],
  ["link_s",     "Spur: grösste Pause (s)"],
  ["on_s",       "Licht brennt nach der letzten Meldung im Polygon (s)"],
  ["start_off_min", "Aktive Zeit: Beginn, Minuten nach Sonnenuntergang"],
  ["end_min",    "Aktive Zeit: Ende (Uhrzeit, z. B. 24:00 oder 01:30)", "time"],
];
function visMinToHHMM(m){ return `${String(Math.floor(m/60)).padStart(2,"0")}:${String(m%60).padStart(2,"0")}`; }
function visClock(t){ const d = new Date(t*1000); return visMinToHHMM(d.getHours()*60 + d.getMinutes()); }
function visHHMMToMin(s){
  const m = /^(\d{1,2}):?(\d{2})$/.exec(String(s).trim()); if(!m) return NaN;
  let v = (+m[1])*60 + (+m[2]);
  if(v < 720) v += 1440;                                  // vor 12:00 = nach Mitternacht
  return v;
}

function visEl(id){ return document.getElementById(id); }
function visMsg(s, err){ const el = visEl("visMsg"); el.textContent = s; el.style.color = err ? "#e66" : "#9ab"; }
function visTrackColor(id){ return `hsl(${(id*67)%360},80%,58%)`; }
function visSelId(){
  if(VIS.selT0 == null || !VIS.data) return null;
  const tr = VIS.data.tracks.find(t=>t.t0 === VIS.selT0);
  return tr ? tr.id : null;
}
function visDur(s){ return s < 90 ? `${Math.round(s)} s` : s < 5400 ? `${Math.round(s/60)} min` : `${(s/3600).toFixed(1)} h`; }

async function visLoad(){
  try{
    VIS.cfg = await anaJson("/visitor_cfg");
    const t1 = Date.now()/1000;
    VIS.data = await anaJson(`/visitors?t0=${t1 - VIS.hours*3600}&t1=${t1}`);
    visRenderParams(); visRenderGroups(); visRenderInfo(); visRenderList(); visDraw();
  }catch(e){ visMsg("Laden fehlgeschlagen: " + e.message, true); }
}

function visRenderInfo(){
  const d = VIS.data; if(!d) return;
  const nIn = d.events.reduce((a,e)=>a+e[7], 0);
  const tot = d.total || {};
  visEl("visInfo").innerHTML =
    `<b>${d.events.length}</b> Meldungen (${nIn} im Polygon) · <b>${d.tracks.length}</b> Spuren · ` +
    `<b>${d.lights.length}</b> × Licht` + (d.cut ? ` <span style="color:#e96">(gekürzt)</span>` : "") +
    `<br><small>gespeichert: ${tot.n||0} Meldungen` + (tot.t0 ? ` seit ${fmtDT(tot.t0)}` : "") +
    ` · werden nach ${d.keep_days} Tagen gelöscht</small>` +
    (VIS.cfg?.window ? `<br>Aktive Zeit ${VIS.cfg.active ? "<b style='color:#ffd23b'>jetzt</b>" : "nächste"}: ` +
      `${fmtDT(VIS.cfg.window[0])} – ${visClock(VIS.cfg.window[1])}` : "") +
    (d.cfg.poly.length ? "" : `<br><small style="color:#e96">Noch kein Polygon: die Auswertung zählt den ganzen Quadranten.</small>`);
}

function visRenderParams(){
  const c = VIS.cfg; if(!c || visEl("visParams").dataset.done) return;
  visEl("visParams").dataset.done = "1";
  visEl("visParams").innerHTML = VIS_PARAMS.map(([k, l, kind])=>{
    const [lo, hi] = c.limits[k];
    if(kind === "time")
      return `<label>${escapeHtml(l)}</label><input type="text" id="visP_${k}" value="${visMinToHHMM(c.params[k])}" title="Standard ${visMinToHHMM(c.defaults[k])}">`;
    return `<label>${escapeHtml(l)}</label><input type="number" id="visP_${k}" min="${lo}" max="${hi}" step="any" value="${c.params[k]}" title="Standard ${c.defaults[k]}">`;
  }).join("");
}

function visRenderList(){
  const d = VIS.data, el = visEl("visList"); if(!d) return;
  const lights = d.lights;
  const lightOf = id => lights.find(l=>l.track===id);
  const sel = visSelId();
  const rows = d.tracks.slice().sort((a,b)=>b.t0-a.t0).map(tr=>{
    const l = lightOf(tr.id);
    const res = tr.trig ? `💡 schaltet ein (${visDur(l.off - l.on)} an)` : tr.hold ? "💡 hält an" :
                tr.day ? "🌞 ausserhalb der aktiven Zeit" : tr.n_in ? "zu kurz" : "ausserhalb";
    return `<tr data-id="${tr.id}" class="${tr.id===sel?'sel':''}">
      <td><i style="background:${visTrackColor(tr.id)}"></i> ${fmtDT(tr.t0)}</td>
      <td>${visDur(tr.t1-tr.t0)}</td><td>${tr.n}</td><td>${tr.n_in}</td>
      <td>${(tr.x/1000).toFixed(1)} / ${(tr.y/1000).toFixed(1)}</td><td>${res}</td></tr>`;
  }).join("");
  el.innerHTML = `<table><thead><tr><th>Beginn</th><th>Dauer</th><th>Meldungen</th><th>im Polygon</th><th>Ort X/Y [m]</th><th>Ergebnis</th></tr></thead><tbody>${rows}</tbody></table>`
    + (d.tracks.length ? "" : `<div class="hint">— keine Besucher-Meldungen im Zeitraum —</div>`);
  el.querySelectorAll("tr[data-id]").forEach(r=>r.onclick=()=>{
    const tr = d.tracks.find(t=>t.id === +r.dataset.id);
    VIS.selT0 = VIS.selT0===tr.t0 ? null : tr.t0;
    visRenderList(); visDraw();
  });
}

// ---- Karte -------------------------------------------------------------------
function visFit(){
  const cv = visEl("visMap"), W = cv.clientWidth, H = cv.clientHeight;
  const xs = [0, 10000], ys = [-4000, 3000];             // Veranda + vorderer Rasen
  (VIS.data?.events||[]).forEach(e=>{ xs.push(e[3]); ys.push(e[4]); });
  (VIS.cfg?.poly||[]).forEach(([x,y])=>{ xs.push(x); ys.push(y); });
  const [minx,maxx] = minMaxArr(xs), [miny,maxy] = minMaxArr(ys);
  VIS.view = {cx:(minx+maxx)/2, cy:(miny+maxy)/2,
              s: Math.min(W/Math.max(maxx-minx,1000), H/Math.max(maxy-miny,1000)) * 0.9};
}
function visT(cv){
  const v = VIS.view, W = cv.clientWidth, H = cv.clientHeight;
  return {W, H, X: x => W/2 + (x-v.cx)*v.s, Y: y => H/2 - (y-v.cy)*v.s,
          invX: px => v.cx + (px-W/2)/v.s, invY: py => v.cy - (py-H/2)/v.s};
}

function visDraw(){
  const cv = visEl("visMap"); if(!cv || cv.offsetParent === null) return;
  if(!VIS.view) visFit();
  const dpr = devicePixelRatio || 1, T = visT(cv), {W, H} = T;
  cv.width = W*dpr; cv.height = H*dpr;
  const ctx = cv.getContext("2d"); ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.fillStyle = "#0c0c10"; ctx.fillRect(0,0,W,H);
  // Quadrant X>0/Y<0 leicht hinterlegt, Achsen, 1-m-Gitter
  ctx.fillStyle = "#16161f"; ctx.fillRect(T.X(0), T.Y(0), W, H);
  const x0 = Math.floor(T.invX(0)/1000), x1 = Math.ceil(T.invX(W)/1000);
  const y0 = Math.floor(T.invY(H)/1000), y1 = Math.ceil(T.invY(0)/1000);
  if(x1-x0 < 200){
    ctx.strokeStyle = "#1f1f28"; ctx.lineWidth = 1;
    for(let g=x0; g<=x1; g++){ ctx.beginPath(); ctx.moveTo(T.X(g*1000),0); ctx.lineTo(T.X(g*1000),H); ctx.stroke(); }
    for(let g=y0; g<=y1; g++){ ctx.beginPath(); ctx.moveTo(0,T.Y(g*1000)); ctx.lineTo(W,T.Y(g*1000)); ctx.stroke(); }
  }
  ctx.strokeStyle = "#445"; ctx.beginPath(); ctx.moveTo(T.X(0),0); ctx.lineTo(T.X(0),H);
  ctx.moveTo(0,T.Y(0)); ctx.lineTo(W,T.Y(0)); ctx.stroke();
  ctx.fillStyle = "#556"; ctx.font = "11px sans-serif";
  ctx.fillText("Veranda-Quadrant X>0 / Y<0", T.X(0)+6, T.Y(0)+14);
  // Rasen (Hintergrund wie Welt-Karte)
  ctx.fillStyle = "#3a3a48";
  (mapPoints||[]).forEach(p=>ctx.fillRect(T.X(p[0]*1000)-1, T.Y(p[1]*1000)-1, 2, 2));
  // Dome-C1
  const pose = VIS.data?.pose;
  if(pose){
    ctx.fillStyle = colorFor(21,0); ctx.beginPath(); ctx.arc(T.X(pose.x),T.Y(pose.y),5,0,7); ctx.fill();
    ctx.fillStyle = "#aab"; ctx.fillText("Dome-C1", T.X(pose.x)+7, T.Y(pose.y)-6);
  }
  // Polygon (gespeichert bzw. Arbeitskopie)
  const poly = VIS.edit || VIS.cfg?.poly || [];
  if(poly.length){
    ctx.beginPath();
    poly.forEach(([x,y],i)=> i ? ctx.lineTo(T.X(x),T.Y(y)) : ctx.moveTo(T.X(x),T.Y(y)));
    if(poly.length > 2) ctx.closePath();
    ctx.fillStyle = "rgba(255,210,59,0.08)"; if(poly.length > 2) ctx.fill();
    ctx.strokeStyle = VIS.edit ? "#fff" : "#ffd23b"; ctx.lineWidth = 2;
    ctx.setLineDash(VIS.edit ? [6,4] : []); ctx.stroke(); ctx.setLineDash([]);
    if(VIS.edit) poly.forEach(([x,y],i)=>{
      ctx.fillStyle = i===VIS.editSel ? "#ffd23b" : "#fff";
      ctx.fillRect(T.X(x)-4, T.Y(y)-4, 8, 8);
    });
  }
  // Meldungen: Farbe = Spur, ausserhalb des Polygons blass, Auslösung = Ring
  const ev = VIS.data?.events || [], sel = visSelId();
  for(const e of ev){
    if(sel != null && e[6] !== sel) continue;
    ctx.globalAlpha = e[7] ? 0.95 : 0.3;
    ctx.fillStyle = visTrackColor(e[6]);
    ctx.beginPath(); ctx.arc(T.X(e[3]), T.Y(e[4]), 2.5, 0, 7); ctx.fill();
  }
  ctx.globalAlpha = 1;
  for(const e of ev){
    if(e[8] !== 1 || (sel != null && e[6] !== sel)) continue;
    ctx.strokeStyle = "#ffd23b"; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(T.X(e[3]), T.Y(e[4]), 9, 0, 7); ctx.stroke();
  }
}

function visPointer(e){ const r = e.currentTarget.getBoundingClientRect(); return {x:e.clientX-r.left, y:e.clientY-r.top}; }
function visHitVertex(T, p){
  let best = -1, bd = VIS_PT_PX;
  (VIS.edit||[]).forEach(([x,y],i)=>{ const d = Math.hypot(T.X(x)-p.x, T.Y(y)-p.y); if(d < bd){ bd = d; best = i; } });
  return best;
}
function visInsertAt(T, p){
  // neue Ecke in die naechstgelegene Kante einfuegen (ab 3 Ecken), sonst anhaengen
  const poly = VIS.edit;
  if(poly.length < 3) return poly.length;
  let best = poly.length, bd = Infinity;
  for(let i=0; i<poly.length; i++){
    const a = poly[i], b = poly[(i+1)%poly.length];
    const d = distToSeg(p.x, p.y, T.X(a[0]), T.Y(a[1]), T.X(b[0]), T.Y(b[1]));
    if(d < bd){ bd = d; best = i+1; }
  }
  return best;
}

function visEditTools(on){
  visEl("visEditBtn").style.display = on ? "none" : "";
  visEl("visEditTools").style.display = on ? "" : "none";
  visEl("visMap").classList.toggle("editing", on);
}

function visInit(){
  const cv = visEl("visMap"); if(!cv) return;
  cv.addEventListener("pointerdown", e=>{
    const T = visT(cv), p = visPointer(e);
    cv.setPointerCapture(e.pointerId);
    if(VIS.edit){
      const i = visHitVertex(T, p);
      if(e.button === 2){ if(i >= 0){ VIS.edit.splice(i,1); VIS.editSel = -1; visDraw(); } return; }
      if(i >= 0){ VIS.editSel = i; VIS.drag = {vertex:i}; visDraw(); return; }
      if(!e.shiftKey){                                   // Klick ins Leere = neue Ecke
        const at = visInsertAt(T, p);
        VIS.edit.splice(at, 0, [Math.round(T.invX(p.x)), Math.round(T.invY(p.y))]);
        VIS.editSel = at; VIS.drag = {vertex:at}; visDraw(); return;
      }
    }
    VIS.drag = {pan:true, x:p.x, y:p.y, cx:VIS.view.cx, cy:VIS.view.cy};
  });
  cv.addEventListener("pointermove", e=>{
    if(!VIS.drag) return;
    const T = visT(cv), p = visPointer(e);
    if(VIS.drag.vertex != null){
      VIS.edit[VIS.drag.vertex] = [Math.round(T.invX(p.x)), Math.round(T.invY(p.y))];
    } else {
      VIS.view.cx = VIS.drag.cx - (p.x-VIS.drag.x)/VIS.view.s;
      VIS.view.cy = VIS.drag.cy + (p.y-VIS.drag.y)/VIS.view.s;
    }
    visDraw();
  });
  cv.addEventListener("pointerup", ()=>{ VIS.drag = null; });
  cv.addEventListener("contextmenu", e=>e.preventDefault());
  cv.addEventListener("wheel", e=>{
    e.preventDefault();
    const T = visT(cv), p = visPointer(e), wx = T.invX(p.x), wy = T.invY(p.y);
    VIS.view.s *= e.deltaY < 0 ? 1.15 : 1/1.15;
    const T2 = visT(cv);
    VIS.view.cx += wx - T2.invX(p.x); VIS.view.cy += wy - T2.invY(p.y);
    visDraw();
  }, {passive:false});
  cv.addEventListener("dblclick", ()=>{ if(!VIS.edit){ VIS.view = null; visDraw(); } });
  window.addEventListener("resize", ()=>{ if(view === "vis") visDraw(); });
  document.addEventListener("keydown", e=>{
    if(view !== "vis" || !VIS.edit) return;
    if(e.key === "Escape"){ VIS.edit = null; visEditTools(false); visDraw(); }
    if((e.key === "Delete" || e.key === "Backspace") && VIS.editSel >= 0){
      VIS.edit.splice(VIS.editSel, 1); VIS.editSel = -1; visDraw();
    }
  });

  visEl("visHours").onchange = ()=>{ VIS.hours = +visEl("visHours").value; VIS.view = null; visLoad(); };
  visEl("visReload").onclick = ()=>visLoad();
  visEl("visEditBtn").onclick = ()=>{
    VIS.edit = (VIS.cfg?.poly || []).map(p=>p.slice()); VIS.editSel = -1; visEditTools(true); visDraw();
    visMsg("Klick = Ecke setzen (wird in die nächste Kante eingefügt) · Ecke ziehen = verschieben · Rechtsklick/Entf = Ecke löschen · Shift+Ziehen = Karte verschieben · Esc = verwerfen");
  };
  visEl("visEditDiscard").onclick = ()=>{ VIS.edit = null; visEditTools(false); visDraw(); visMsg(""); };
  visEl("visEditSave").onclick = async()=>{
    if(VIS.edit.length < 3){ visMsg("Ein Polygon braucht mindestens 3 Ecken.", true); return; }
    await visSaveCfg({poly: VIS.edit});
    VIS.edit = null; visEditTools(false); visMsg("Polygon gespeichert.");
  };
  visEl("visPolyDel").onclick = async()=>{
    if(!confirm("Auslösepolygon löschen? (Die Auswertung zählt dann wieder den ganzen Quadranten.)")) return;
    VIS.edit = null; visEditTools(false); await visSaveCfg({poly: []}); visMsg("Polygon gelöscht.");
  };
  visEl("visParamSave").onclick = async()=>{
    const params = {};
    for(const [k, , kind] of VIS_PARAMS){
      params[k] = kind === "time" ? visHHMMToMin(visEl("visP_"+k).value) : +visEl("visP_"+k).value;
      if(Number.isNaN(params[k])){ visMsg("Ungültiger Wert: " + k, true); return; }
    }
    await visSaveCfg({params});
    visEl("visParams").dataset.done = ""; visRenderParams();
    visMsg("Parameter übernommen, Auswertung neu gerechnet.");
  };
  visEl("visParamDef").onclick = async()=>{
    await visSaveCfg({params: VIS.cfg.defaults});
    visEl("visParams").dataset.done = ""; visRenderParams(); visMsg("Standardwerte gesetzt.");
  };
  visEl("visArmOn").onclick = ()=>visArm(1);
  visEl("visArmOff").onclick = ()=>visArm(0);
  visEl("visDevReload").onclick = async()=>{
    await postCmd(VIS_DEV, VIS_CMD_RELOAD, 0); visMsg("Steuergerät lädt die Konfiguration neu …");
  };
  visEl("visGroupSave").onclick = async()=>{
    const home = visEl("visHome").value.split(",").map(x=>x.trim()).filter(Boolean);
    const light = [...document.querySelectorAll("#visLight input:checked")].map(x=>x.value);
    if(!light.length){ visMsg("Mindestens eine Gruppe als Abschreck-Licht wählen.", true); return; }
    await visSaveCfg({home_groups: home, light_groups: light});
    visEl("visLight").dataset.done = ""; visRenderGroups();
    visMsg("Gruppen übernommen (Gerät holt sie spätestens in 15 min, sofort mit „Gerät lädt Konfiguration“).");
  };
  visEl("visReset").onclick = async()=>{
    if(!confirm("ALLE Besucher-Meldungen löschen? (Polygon, Parameter und alle übrigen Daten bleiben.)")) return;
    const r = await anaJson("/visitor_reset", {method:"POST"});
    VIS.selT0 = null; visMsg(`${r.deleted} Besucher-Meldungen gelöscht.`); visLoad();
  };
}

async function visSaveCfg(body){
  try{
    await anaJson("/visitor_cfg", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(body)});
    await visLoad();
  }catch(e){ visMsg("Speichern fehlgeschlagen: " + e.message, true); }
}

function visRenderGroups(){
  const c = VIS.cfg, el = visEl("visLight"); if(!c || el.dataset.done) return;
  el.dataset.done = "1";
  visEl("visHome").value = c.home_groups.join(", ");
  const names = [...c.home_groups, ...c.light_groups.filter(g=>!c.home_groups.includes(g))];
  el.innerHTML = names.map(g=>`<label class="visChk"><input type="checkbox" value="${escapeHtml(g)}" ${c.light_groups.includes(g)?"checked":""}> ${escapeHtml(g)}</label>`).join("");
}

// Scharf/Aus: absolut setzen (cmdSetSetting), nie umschalten - ein verlorenes
// oder doppeltes Paket aendert so nichts am Ergebnis.
async function visArm(on){
  await postCmd(VIS_DEV, CMD_SET_SETTING, (VIS_STG_ARMED<<1) | on);
  visMsg(on ? "Scharf schalten gesendet …" : "Ausschalten gesendet …");
  setTimeout(visRenderArm, 2500);
}
async function visRenderArm(){
  let st;
  try{ st = await (await fetch("state")).json(); }catch(e){ return; }
  const s = (st.settings||{})[String(VIS_DEV)], el = visEl("visArm");
  if(!s){ el.className = ""; el.innerHTML = "Steuergerät #22 <b>offline</b> (kein HB)"; }
  else {
    const on = (s.val >> VIS_STG_ARMED) & 1;
    el.className = on ? "on" : "";
    el.innerHTML = `Steuergerät: <b>${on ? "SCHARF" : "aus"}</b> <small style="color:#999">(HB vor ${Math.round(s.age)} s)</small>`;
  }
  const rows = (st.debug||[]).filter(r=>/Besucherlicht/.test(r.msg)).slice(-12).reverse();
  visEl("visDev").innerHTML = rows.map(r=>`<div>${fmtTime(r.t)} ${escapeHtml(r.msg.replace(/^Besucherlicht:?\s*/,""))}</div>`).join("")
    || `<div class="hint">keine Meldungen des Steuergeräts</div>`;
}

function visShow(){
  visLoad();
  visRenderArm();
  clearInterval(VIS.armTimer);
  VIS.armTimer = setInterval(()=>{
    if(view !== "vis"){ clearInterval(VIS.armTimer); return; }
    visRenderArm();
  }, 3000);
  clearInterval(VIS.timer);                               // live nachladen, solange der Tab offen ist
  VIS.timer = setInterval(()=>{
    if(view !== "vis"){ clearInterval(VIS.timer); return; }
    if(!VIS.edit && !VIS.drag) visLoad();
  }, 10000);
}
setTimeout(visInit, 0);
