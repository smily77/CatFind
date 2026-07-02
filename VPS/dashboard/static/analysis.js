// analysis.js — Analyse-Tab: Langzeit-Aufnahme durchscrollen, Modell-Overlay,
// Labeling. Gehört zu index.html (nutzt dessen globale colorFor/fmtTime/
// escapeHtml/mapPoints). Serverseite: /rec /density /adata /amodel /aparams
// /alabel(s). Zeiten sind Unix-Sekunden (Serverzeit, ms-genau).
"use strict";

const LBL_COLORS = {
  "Katze":"#2ecc71", "Einzelereignis":"#8a8f98", "Insekt":"#e67e22",
  "Vegetation":"#7a9e35", "Vogel":"#3ad0d0", "Sonne/Lidar":"#e6c522",
  "Regen/Sturm":"#3a7bd5", "unbekannt":"#b06fd8"
};
const ANA = {
  win: null,            // {t0,t1} betrachtetes Fenster
  dens: null,           // /density über die ganze Aufnahme
  data: null,           // /adata des Fensters
  model: null,          // /amodel des Fensters
  labels: [],
  rec: null,
  modelOn: true,
  follow: false,
  sensorOff: new Set(), // "sender.sensor" ausgeblendet
  view: null,           // Karten-Transform {cx,cy,s} (Welt-mm -> px)
  selTrack: null,
  sel: null,            // Zeit-Selektion auf der Zeitleiste {t0,t1}
  fetchTimer: null,
};

function anaEl(id){ return document.getElementById(id); }
function fmtDur(s){
  if(s < 120) return Math.round(s) + " s";
  if(s < 7200) return Math.round(s/60) + " min";
  if(s < 172800) return (s/3600).toFixed(1) + " h";
  return (s/86400).toFixed(1) + " Tage";
}
function fmtDT(t){
  const d = new Date(t*1000);
  return d.toLocaleDateString("de-CH",{day:"2-digit",month:"2-digit"}) + " " +
         d.toLocaleTimeString("de-CH");
}

// ------------------------------------------------------------- Daten holen

async function anaJson(url, opts){
  const r = await fetch(url, opts);
  if(!r.ok) throw new Error((await r.json().catch(()=>({}))).error || r.statusText);
  return r.json();
}

async function anaRefreshRec(){
  try{ ANA.rec = await anaJson("/rec"); }catch(e){ ANA.rec = null; }
  renderRec();
}

async function anaRefreshDensity(){
  try{ ANA.dens = await anaJson("/density?bins=700"); }catch(e){ ANA.dens = null; }
  drawTimeline();
}

function anaScheduleFetch(){
  clearTimeout(ANA.fetchTimer);
  ANA.fetchTimer = setTimeout(anaFetchWindow, 300);
  drawTimeline();      // Fensterrahmen sofort nachführen
  renderWinInfo();
}

async function anaFetchWindow(){
  if(!ANA.win) return;
  const q = `t0=${ANA.win.t0}&t1=${ANA.win.t1}`;
  try{ ANA.data = await anaJson(`/adata?${q}`); }catch(e){ ANA.data = {events:[],total:0,stride:1}; }
  if(ANA.modelOn){
    try{ ANA.model = await anaJson(`/amodel?${q}`); anaEl("anaModelErr").textContent=""; }
    catch(e){ ANA.model = null; anaEl("anaModelErr").textContent = "Modell: " + e.message; }
  } else ANA.model = null;
  try{ ANA.labels = (await anaJson(`/alabels?${q}`)).labels; }catch(e){ ANA.labels = []; }
  ANA.selTrack = null;
  renderSensors(); renderTracks(); renderLabels(); drawAnaMap(); drawTimeline(); renderWinInfo();
}

// ------------------------------------------------------------- Fenster-Navigation

function anaShift(frac){
  const w = ANA.win, dt = (w.t1 - w.t0) * frac;
  w.t0 += dt; w.t1 += dt; ANA.follow = false; anaScheduleFetch();
}
function anaZoomTime(f, center){
  const w = ANA.win, c = center ?? (w.t0 + w.t1) / 2;
  let half0 = (c - w.t0) * f, half1 = (w.t1 - c) * f;
  const span = half0 + half1;
  if(span < 2){ half0 *= 2/span; half1 *= 2/span; }             // min 2 s
  if(span > 45*86400){ half0 *= 45*86400/span; half1 *= 45*86400/span; }
  w.t0 = c - half0; w.t1 = c + half1; anaScheduleFetch();
}
function anaNow(){
  const w = ANA.win, span = w.t1 - w.t0, now = Date.now()/1000;
  w.t1 = now; w.t0 = now - span; anaScheduleFetch();
}

// ------------------------------------------------------------- Zeitleiste

function tlX(t, W){
  const d = ANA.dens; if(!d) return 0;
  return (t - d.t0) / Math.max(d.t1 - d.t0, 1e-6) * W;
}
function tlT(x, W){
  const d = ANA.dens;
  return d.t0 + x / W * (d.t1 - d.t0);
}

function drawTimeline(){
  const cv = anaEl("anaTl"); if(!cv || cv.offsetParent === null) return;
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W*dpr; cv.height = H*dpr;
  const ctx = cv.getContext("2d"); ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.fillStyle = "#0c0c10"; ctx.fillRect(0,0,W,H);
  const d = ANA.dens;
  if(!d || d.t1 <= d.t0){ ctx.fillStyle="#555"; ctx.fillText("keine Aufnahme-Daten", 10, 20); return; }
  const densH = H - 16;
  // Dichte je Sender gestapelt (log-Skala, damit Bursts die Nächte nicht plattdrücken)
  const senders = Object.keys(d.per_sender);
  const totals = new Array(d.bins).fill(0);
  senders.forEach(s => d.per_sender[s].forEach((n,i)=> totals[i]+=n));
  const maxN = Math.max(1, ...totals);
  const bw = W / d.bins;
  for(let i=0;i<d.bins;i++){
    if(!totals[i]) continue;
    const h = Math.max(2, Math.log(1+totals[i]) / Math.log(1+maxN) * (densH-4));
    let y = densH;
    for(const s of senders){
      const n = d.per_sender[s][i]; if(!n) continue;
      const hs = h * n / totals[i];
      ctx.fillStyle = colorFor(+s, 0);
      ctx.fillRect(i*bw, y-hs, Math.max(bw,1), hs);
      y -= hs;
    }
  }
  // verworfene Burst-Events (Manager-Drop-Zähler) als rote Marker oben
  ctx.fillStyle = "#f33";
  d.drops.forEach((n,i)=>{ if(n) ctx.fillRect(i*bw, 0, Math.max(bw,1), 3); });
  // Labels als Bänder unten
  for(const l of (ANA.labels||[])){
    ctx.fillStyle = (LBL_COLORS[l.label]||"#999") + "cc";
    const x0 = Math.max(0, tlX(l.t0,W)), x1 = Math.min(W, tlX(l.t1,W));
    ctx.fillRect(x0, H-14, Math.max(x1-x0,2), 12);
  }
  // Zeit-Selektion (Shift+Ziehen)
  if(ANA.sel){
    ctx.fillStyle = "#e6c52233";
    const x0 = tlX(ANA.sel.t0,W), x1 = tlX(ANA.sel.t1,W);
    ctx.fillRect(Math.min(x0,x1), 0, Math.abs(x1-x0), H);
    ctx.strokeStyle = "#e6c522"; ctx.strokeRect(Math.min(x0,x1)+.5, .5, Math.abs(x1-x0), H-1);
  }
  // aktuelles Fenster
  if(ANA.win){
    const x0 = tlX(ANA.win.t0,W), x1 = tlX(ANA.win.t1,W);
    ctx.fillStyle = "#2d6cdf22"; ctx.fillRect(x0, 0, Math.max(x1-x0,2), H);
    ctx.strokeStyle = "#2d6cdf"; ctx.lineWidth = 1.5;
    ctx.strokeRect(x0+.5, .5, Math.max(x1-x0,2), H-1);
  }
  // CatDetected-Marker des Modells
  if(ANA.model) for(const tr of ANA.model.tracks) if(tr.confirmed){
    ctx.fillStyle = "#2ecc71";
    const x = tlX(tr.t_confirm, W);
    ctx.beginPath(); ctx.moveTo(x, densH); ctx.lineTo(x-4, densH-8); ctx.lineTo(x+4, densH-8); ctx.fill();
  }
  // Achsenbeschriftung
  ctx.fillStyle = "#778"; ctx.font = "10px system-ui";
  ctx.fillText(fmtDT(d.t0), 4, 10);
  const s1 = fmtDT(d.t1); ctx.fillText(s1, W - ctx.measureText(s1).width - 4, 10);
}

function tlBind(){
  const cv = anaEl("anaTl");
  let drag = null;
  cv.addEventListener("mousedown", e => {
    const W = cv.clientWidth, t = tlT(e.offsetX, W);
    if(e.shiftKey){ drag = {mode:"sel", start:t}; ANA.sel = {t0:t, t1:t}; }
    else {
      const span = ANA.win.t1 - ANA.win.t0;
      ANA.win.t0 = t - span/2; ANA.win.t1 = t + span/2; ANA.follow = false;
      drag = {mode:"move"}; anaScheduleFetch();
    }
  });
  cv.addEventListener("mousemove", e => {
    if(!drag) return;
    const W = cv.clientWidth, t = tlT(e.offsetX, W);
    if(drag.mode==="sel"){ ANA.sel = {t0:Math.min(drag.start,t), t1:Math.max(drag.start,t)}; drawTimeline(); }
    else {
      const span = ANA.win.t1 - ANA.win.t0;
      ANA.win.t0 = t - span/2; ANA.win.t1 = t + span/2; anaScheduleFetch();
    }
  });
  window.addEventListener("mouseup", ()=> drag=null);
  cv.addEventListener("wheel", e => {
    e.preventDefault();
    anaZoomTime(e.deltaY > 0 ? 1.3 : 1/1.3, tlT(e.offsetX, cv.clientWidth));
  }, {passive:false});
}

// ------------------------------------------------------------- Karte

function anaFit(){
  const xs=[], ys=[];
  (mapPoints||[]).forEach(p=>{ xs.push(p[0]*1000); ys.push(p[1]*1000); });
  (ANA.data?.events||[]).forEach(e=>{ if(e.wv===1){ xs.push(e.wx); ys.push(e.wy); } });
  if(xs.length < 2){ ANA.view = {cx:0, cy:5000, s:0.05}; return; }
  const minx=Math.min(...xs), maxx=Math.max(...xs), miny=Math.min(...ys), maxy=Math.max(...ys);
  const cv = anaEl("anaMap"), W = cv.clientWidth, H = cv.clientHeight;
  const s = Math.min(W/Math.max(maxx-minx,1000), H/Math.max(maxy-miny,1000)) * 0.9;
  ANA.view = {cx:(minx+maxx)/2, cy:(miny+maxy)/2, s};
}

function drawAnaMap(){
  const cv = anaEl("anaMap"); if(!cv || cv.offsetParent === null) return;
  if(!ANA.view) anaFit();
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W*dpr; cv.height = H*dpr;
  const ctx = cv.getContext("2d"); ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.fillStyle = "#0c0c10"; ctx.fillRect(0,0,W,H);
  const v = ANA.view;
  const X = wx => W/2 + (wx - v.cx) * v.s;
  const Y = wy => H/2 - (wy - v.cy) * v.s;
  // Gitter (1 m bzw. 5 m)
  const step = v.s*1000 > 25 ? 1000 : 5000;
  const wx0 = v.cx - W/2/v.s, wx1 = v.cx + W/2/v.s;
  const wy0 = v.cy - H/2/v.s, wy1 = v.cy + H/2/v.s;
  ctx.strokeStyle = "#1d1d24"; ctx.lineWidth = 1;
  for(let gx=Math.ceil(wx0/step)*step; gx<=wx1; gx+=step){ ctx.beginPath(); ctx.moveTo(X(gx),0); ctx.lineTo(X(gx),H); ctx.stroke(); }
  for(let gy=Math.ceil(wy0/step)*step; gy<=wy1; gy+=step){ ctx.beginPath(); ctx.moveTo(0,Y(gy)); ctx.lineTo(W,Y(gy)); ctx.stroke(); }
  // RasenKarte
  if(mapPoints) { ctx.fillStyle="#556"; mapPoints.forEach(p=>ctx.fillRect(X(p[0]*1000)-1.5, Y(p[1]*1000)-1.5, 3, 3)); }
  // Events des Fensters (Alter im Fenster -> Helligkeit)
  const w = ANA.win, span = Math.max(w.t1-w.t0, 1e-6);
  for(const e of (ANA.data?.events||[])){
    if(e.wv !== 1) continue;
    if(ANA.sensorOff.has(e.sender + "." + e.sensor)) continue;
    const a = 0.25 + 0.75 * (e.t - w.t0) / span;
    ctx.globalAlpha = Math.max(0.15, Math.min(1, a));
    ctx.fillStyle = colorFor(e.sender, e.sensor);
    ctx.beginPath(); ctx.arc(X(e.wx), Y(e.wy), 2.5, 0, 7); ctx.fill();
  }
  ctx.globalAlpha = 1;
  // Modell-Tracks
  if(ANA.model) for(const tr of ANA.model.tracks){
    if(tr.pts.length < 2 && !tr.confirmed) continue;
    const hot = ANA.selTrack === tr.id;
    ctx.strokeStyle = tr.confirmed ? "#2ecc71" : "#888a";
    ctx.lineWidth = hot ? 3.5 : (tr.confirmed ? 2 : 1);
    ctx.beginPath();
    tr.pts.forEach((p,i)=> i ? ctx.lineTo(X(p[1]),Y(p[2])) : ctx.moveTo(X(p[1]),Y(p[2])));
    ctx.stroke();
    if(tr.confirmed){
      const cp = tr.pts.reduce((b,p)=> Math.abs(p[0]-tr.t_confirm) < Math.abs(b[0]-tr.t_confirm) ? p : b, tr.pts[0]);
      ctx.strokeStyle = "#2ecc71"; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(X(cp[1]), Y(cp[2]), 9, 0, 7); ctx.stroke();
      ctx.fillStyle = "#2ecc71"; ctx.font = "bold 11px system-ui";
      ctx.fillText("CatDetected #" + tr.id, X(cp[1]) + 12, Y(cp[2]) - 6);
    }
  }
  // Massstab
  ctx.fillStyle = "#778"; ctx.font = "11px system-ui";
  ctx.fillText((step/1000) + " m Raster", 8, H-8);
}

function mapBind(){
  const cv = anaEl("anaMap");
  let drag = null;
  cv.addEventListener("mousedown", e => { drag = {x:e.offsetX, y:e.offsetY}; });
  cv.addEventListener("mousemove", e => {
    if(!drag) return;
    ANA.view.cx -= (e.offsetX - drag.x) / ANA.view.s;
    ANA.view.cy += (e.offsetY - drag.y) / ANA.view.s;
    drag = {x:e.offsetX, y:e.offsetY};
    drawAnaMap();
  });
  window.addEventListener("mouseup", ()=> drag=null);
  cv.addEventListener("wheel", e => {
    e.preventDefault();
    const f = e.deltaY > 0 ? 1/1.25 : 1.25;
    const v = ANA.view, W = cv.clientWidth, H = cv.clientHeight;
    const wx = v.cx + (e.offsetX - W/2)/v.s, wy = v.cy - (e.offsetY - H/2)/v.s;
    v.s *= f;
    v.cx = wx - (e.offsetX - W/2)/v.s;
    v.cy = wy + (e.offsetY - H/2)/v.s;
    drawAnaMap();
  }, {passive:false});
  cv.addEventListener("dblclick", ()=>{ anaFit(); drawAnaMap(); });
}

// ------------------------------------------------------------- Panels

function renderRec(){
  const el = anaEl("anaRec"); if(!el) return;
  const r = ANA.rec;
  if(!r){ el.innerHTML = "<span style='color:#c66'>keine Verbindung</span>"; return; }
  const mb = (r.bytes/1048576).toFixed(1);
  el.innerHTML =
    `<button class="sw ${r.on?'on':'off'}" id="anaRecBtn">${r.on?'● REC':'⏸ Pause'}</button>
     <span style="color:#888;font-size:12px">${r.rows.toLocaleString("de-CH")} Events · ${mb} MB` +
     (r.t_min ? ` · seit ${fmtDT(r.t_min)}` : "") + `</span>`;
  anaEl("anaRecBtn").onclick = async ()=>{
    ANA.rec = await anaJson("/rec", {method:"POST",
      headers:{"Content-Type":"application/json"}, body:JSON.stringify({on: r.on?0:1})});
    renderRec();
  };
}

function renderWinInfo(){
  const el = anaEl("anaWinInfo"); if(!el || !ANA.win) return;
  const span = ANA.win.t1 - ANA.win.t0;
  const tot = ANA.data ? ` · ${ANA.data.total.toLocaleString("de-CH")} Events` +
              (ANA.data.stride>1 ? ` (1/${ANA.data.stride} gezeigt)` : "") : "";
  el.textContent = `${fmtDT(ANA.win.t0)} — ${fmtDT(ANA.win.t1)} (${fmtDur(span)})${tot}`;
}

function renderSensors(){
  const el = anaEl("anaSensors"); if(!el) return;
  const cnt = {};
  for(const e of (ANA.data?.events||[])){
    const k = e.sender + "." + e.sensor;
    cnt[k] = cnt[k] || {n:0, nw:0, sender:e.sender, sensor:e.sensor};
    cnt[k].n++; if(e.wv===1) cnt[k].nw++;
  }
  const storms = ANA.model?.storms || {};
  el.innerHTML = Object.keys(cnt).sort().map(k=>{
    const c = cnt[k], off = ANA.sensorOff.has(k);
    const st = (storms[String(c.sender)]||[]).length ? " ⚡" : "";
    return `<label class="anaSens${off?' off':''}" data-k="${k}">
      <input type="checkbox" ${off?'':'checked'}>
      <i style="background:${colorFor(c.sender,c.sensor)}"></i>#${k}${st}
      <small>${c.n}${c.nw<c.n?` (${c.n-c.nw} o.Welt)`:''}</small></label>`;
  }).join("") || "<div class='hint'>— keine Events im Fenster —</div>";
  el.querySelectorAll(".anaSens input").forEach(inp=>{
    inp.onchange = e=>{
      const k = e.target.closest(".anaSens").dataset.k;
      e.target.checked ? ANA.sensorOff.delete(k) : ANA.sensorOff.add(k);
      drawAnaMap();
    };
  });
}

function renderTracks(){
  const el = anaEl("anaTracks"); if(!el) return;
  if(!ANA.model){ el.innerHTML = "<div class='hint'>— Modell aus / kein Ergebnis —</div>"; return; }
  const trs = ANA.model.tracks.filter(t=>t.n>=2 || t.confirmed)
              .sort((a,b)=>(b.confirmed-a.confirmed) || (b.n-a.n)).slice(0,80);
  el.innerHTML = `<div class="hint">${ANA.model.n_confirmed}× CatDetected / ${ANA.model.tracks.length} Tracks</div>` +
    trs.map(t=>`<div class="anaTrack${t.confirmed?' cat':''}${ANA.selTrack===t.id?' sel':''}" data-id="${t.id}">
      ${t.confirmed?'🐱':'·'} #${t.id} n=${t.n} ${fmtDur(t.t1-t.t0)} ${(t.net_mm/1000).toFixed(1)}m
      ${t.v_mean? (t.v_mean/1000).toFixed(1)+'m/s':''}
      <small>${t.confirmed ? t.reasons.join(",") : escapeHtml(t.reject||"")}</small></div>`).join("");
  el.querySelectorAll(".anaTrack").forEach(d=>{
    d.onclick = ()=>{
      const id = +d.dataset.id;
      ANA.selTrack = (ANA.selTrack===id) ? null : id;
      const tr = ANA.model.tracks.find(t=>t.id===id);
      if(tr && ANA.selTrack!==null){
        ANA.view.cx = tr.pts.reduce((s,p)=>s+p[1],0)/tr.pts.length;
        ANA.view.cy = tr.pts.reduce((s,p)=>s+p[2],0)/tr.pts.length;
      }
      renderTracks(); drawAnaMap();
    };
  });
}

function renderLabels(){
  const el = anaEl("anaLabels"); if(!el) return;
  el.innerHTML = (ANA.labels||[]).map(l=>
    `<div class="anaLbl"><i style="background:${LBL_COLORS[l.label]||'#999'}"></i>
     ${escapeHtml(l.label)} <small>${fmtDT(l.t0)} · ${fmtDur(l.t1-l.t0)}${l.sender>=0?` · #${l.sender}`:''}</small>
     <button data-id="${l.id}">✕</button></div>`).join("") || "<div class='hint'>— keine Labels im Fenster —</div>";
  el.querySelectorAll(".anaLbl button").forEach(b=>{
    b.onclick = async ()=>{
      await anaJson("/alabel_del",{method:"POST",headers:{"Content-Type":"application/json"},
                    body:JSON.stringify({id:+b.dataset.id})});
      anaFetchWindow(); anaRefreshDensity();
    };
  });
}

async function anaAddLabel(){
  const label = anaEl("anaLblSel").value;
  const sender = +anaEl("anaLblSender").value;
  const r = ANA.sel || ANA.win;
  await anaJson("/alabel",{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({t0:r.t0, t1:r.t1, sender, label})});
  ANA.sel = null;
  anaFetchWindow();
}

// ------------------------------------------------------------- Parameter

async function anaParams(){
  const ov = anaEl("anaParamsOv");
  const p = await anaJson("/aparams");
  anaEl("anaParamsTxt").value = JSON.stringify(p, null, 2);
  ov.style.display = "flex";
  anaEl("anaParamsSave").onclick = async ()=>{
    try{
      const parsed = JSON.parse(anaEl("anaParamsTxt").value);
      await anaJson("/aparams",{method:"POST",headers:{"Content-Type":"application/json"},
                    body:JSON.stringify(parsed)});
      ov.style.display = "none";
      anaFetchWindow();
    }catch(e){ alert("Ungültig: " + e.message); }
  };
  anaEl("anaParamsClose").onclick = ()=> ov.style.display = "none";
}

// ------------------------------------------------------------- Einstieg

let anaInit = false;
async function anaShow(){
  if(!anaInit){
    anaInit = true;
    tlBind(); mapBind();
    anaEl("anaB1").onclick = ()=>anaShift(-0.8);
    anaEl("anaB2").onclick = ()=>anaShift(-0.25);
    anaEl("anaB3").onclick = ()=>anaShift(0.25);
    anaEl("anaB4").onclick = ()=>anaShift(0.8);
    anaEl("anaZi").onclick = ()=>anaZoomTime(1/1.6);
    anaEl("anaZo").onclick = ()=>anaZoomTime(1.6);
    anaEl("anaBNow").onclick = anaNow;
    anaEl("anaModelChk").onchange = e=>{ ANA.modelOn = e.target.checked; anaFetchWindow(); };
    anaEl("anaFollowChk").onchange = e=>{ ANA.follow = e.target.checked; if(ANA.follow) anaNow(); };
    anaEl("anaBParams").onclick = anaParams;
    anaEl("anaBLabel").onclick = anaAddLabel;
    anaEl("anaLblSel").innerHTML = Object.keys(LBL_COLORS).map(l=>`<option>${l}</option>`).join("");
    window.addEventListener("resize", ()=>{ if(view==="ana"){ drawTimeline(); drawAnaMap(); } });
    setInterval(async ()=>{
      if(view!=="ana") return;
      await anaRefreshRec();
      if(ANA.follow){ await anaRefreshDensity(); anaNow(); }
    }, 5000);
  }
  await anaRefreshRec();
  await anaRefreshDensity();
  if(!ANA.win){
    const d = ANA.dens;
    const t1 = d ? d.t1 : Date.now()/1000;
    ANA.win = {t0: t1 - 3600, t1};          // Start: letzte Stunde
    // Sender-Liste fürs Label-Ziel
    anaEl("anaLblSender").innerHTML = `<option value="-1">alle Sensoren</option>` +
      Object.keys(d?.per_sender||{}).map(s=>`<option value="${s}">nur #${s}</option>`).join("");
  }
  anaFetchWindow();
}
