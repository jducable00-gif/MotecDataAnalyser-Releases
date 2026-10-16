//---------------------------------------------------------------------------
// TabletReportPage.h — Phase C (Tablet Report Viewer plan, docs/TABLET_REPORT_VIEWER_PLAN.md §C1-C5).
// Self-contained HTML document served at GET / by the embedded HTTP server.
// Mobile-first: one-column scroll; system font stack; no external CDN/framework.
// Vanilla DOM + Canvas 2D so it works fully offline once loaded over LAN.
// Compiled into the exe → no extra file beside .exe (portable single-exe model).
//---------------------------------------------------------------------------

#ifndef TABLETREPORTPAGE_H
#define TABLETREPORTPAGE_H

#include <string>

inline const std::string& tabletIndexHtml() {
    static const std::string page = R"XHTMLDOC(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=5.0">
<title>Motec Data Analyser - Tablet Report</title>
<style>
:root{--bg:#f4f6f9;--card:#fff;--text:#2c3e50;--muted:#7f8c8d;--accent:#2980b9;
      --ok:#27ae60;--warn:#f39c12;--crit:#e74c3c;--border:#dde3ea}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
     background:var(--bg);color:var(--text);line-height:1.5;padding-bottom:3rem}
header{background:#2c3e50;color:#fff;padding:.8rem 1rem;text-align:center;font-size:1.1rem}
main{max-width:720px;margin:0 auto;padding:1rem .6rem}

/* --- Section cards (C2) --- */
.card{background:var(--card);border-radius:10px;box-shadow:0 1px 4px rgba(0,0,0,.08);
      margin-bottom:.9rem;overflow:hidden;border-left:4px solid var(--accent)}
.card.rec{border-left-color:var(--warn)}
details summary{cursor:pointer;padding:.75rem 1rem;font-weight:600;display:flex;align-items:center;gap:.5rem}
.badge{display:inline-block;font-size:.7rem;font-weight:700;text-transform:uppercase;letter-spacing:.4px;
       padding:2px 8px;border-radius:99px;color:#fff;background:var(--muted)}
.badge.rec{background:var(--warn);color:#3d2c00}
.card-body{padding:0 1rem .85rem;font-size:.94rem;white-space:pre-wrap}

/* --- Tyre heat map (C3) --- */
.tyre-container{display:grid;grid-template-columns:repeat(2,1fr);gap:1.5rem;padding:0 1rem .85rem}
@media(max-width:420px){.tyre-container{grid-template-columns:1fr}}
.tyre-group{text-align:center;display:flex;flex-direction:column;align-items:center;gap:.4rem}
.tyre-header{font-weight:700;color:var(--text);line-height:1.2}
.tyre-pressure{font-size:.85rem;color:var(--muted);margin-bottom:.5rem}
.tyre-bars{display:flex;gap:4px;align-items:flex-end;height:60px;margin-bottom:.5rem}
.tyre-bar{width:22px;height:100%;border-radius:3px 3px 0 0;transition:background .3s}
.tyre-vals{font-size:.75rem;color:var(--muted);font-family:monospace}
.tyre-cell{text-align:center;border-radius:6px;padding:.45rem .3rem;font-size:.9rem;font-weight:700;color:#fff}

/* --- Brake gauges (C4) --- */
.brake-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:1.5rem;margin-top:.7rem;padding:0 .9rem .9rem}
@media(max-width:420px){.brake-grid{grid-template-columns:1fr}}
.brake-box{text-align:center}
.brake-title{font-size:.78rem;font-weight:600;color:var(--muted);margin-bottom:.3rem}

/* --- Damper histograms (C5) --- */
.damper-grid{display:grid;grid-template-columns:1fr 1fr;gap:.8rem;margin-top:.7rem;padding:0 .9rem .9rem}
@media(max-width:420px){.damper-grid{grid-template-columns:1fr}}
.damper-box{text-align:center}
.damper-title{font-size:.78rem;font-weight:600;color:var(--muted);margin-bottom:.3rem}
.damper-controls{display:flex;gap:8px;padding:0 1rem .5rem;margin-top:.7rem}
.damper-btn{cursor:pointer;border:none;font-size:.7rem;font-weight:700;text-transform:uppercase;padding:4px 12px;border-radius:99px;background:var(--muted);color:#fff;transition:opacity .2s}
.damper-btn.active{background:var(--accent)}
.damper-btn:hover{opacity:.8}

/* --- Empty state (plan §7) --- */
.empty-notice{background:#fff;border-radius:12px;padding:2.5rem 1.4rem;text-align:center;
              box-shadow:0 1px 6px rgba(0,0,0,.08);margin-top:1.5rem}
.empty-notice h2{font-size:1.3rem;margin-bottom:.7rem;color:var(--accent)}

/* --- Footer / status (C1) --- */
footer{text-align:center;font-size:.76rem;color:var(--muted);padding:1rem .8rem 0}
.hidden{display:none!important}
</style>
</head>
<body>
<header>Motec Data Analyser - Session Report</header>
<main id="content">
  <div class="empty-notice" id="emptyNotice"><h2>No session data yet</h2><p>Analyze a file on the PC and this page will update automatically.</p></div>
  <section id="sectionsWrap"></section>
  <section id="tyreSection" class="card hidden">
    <details open><summary>Tyre Temperatures & Pressures</summary>
      <div class="tyre-container" id="tyreGrid"></div>
    </details>
  </section>
  <section id="brakeSection" class="card hidden">
    <details open><summary>Brake Temperatures</summary>
      <div class="brake-grid" id="brakeWrap"></div>
    </details>
  </section>
  <section id="engineSection" class="card hidden">
    <details open><summary>Engine Temperatures</summary>
      <div class="brake-grid" id="engineWrap"></div>
    </details>
  </section>
  <section id="damperSection" class="card hidden">
    <details open><summary>Damper Velocity Histograms (mm/s)</summary>
      <div class="damper-controls">
        <button id="btnDamperGrid" class="damper-btn active">Grid</button>
        <button id="btnDamperCombined" class="damper-btn">Combined</button>
      </div>
      <div class="damper-grid" id="damperGrid"></div>
      <div id="damperCombinedWrap" class="hidden" style="padding:0 1rem .85rem; text-align:center;">
        <canvas id="damperCombinedCanvas" width="640" height="320" style="max-width:100%; height:auto;"></canvas>
        <div id="damperStatsTable" style="margin-top:1rem; font-family:monospace; font-size:.85rem; color:var(--text); display:grid; grid-template-columns: 1fr 1fr 1fr; gap:10px; border-top:1px solid var(--border); padding-top:1rem;">
        </div>
      </div>
    </details>
  </section>
</main>
<footer>Last updated: <span id="lastUpdated">—</span></footer>

<script>
"use strict";
var lastVersion = -1;
var POLL_MS = 3000;

/* ---------- helpers ---------- */
function esc(s){return s.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")}

// Brake bar colour: blue→orange→red over 200–600 °C.
function brakeColor(t){
  if(t==null||isNaN(t))return"#95a5a6";
  var f=Math.max(0,Math.min(1,(t-200)/400));
  var r,g,b;
  if(f<0.5){var u=f/0.5;r=Math.round(33+(243-33)*u);g=Math.round(150+(156-150)*u);b=Math.round(243+(18-243)*u);}
  else{var v=(f-0.5)/0.5;r=Math.round(243*(1-v)+231*v);g=Math.round(156*(1-v)+(76)*(v));b=Math.round(18*(1-v)+60*v);}
  return"rgb("+r+","+g+","+b+")";
}

/* ---------- C2: text section cards ---------- */
function renderSections(data){
  var wrap=document.getElementById("sectionsWrap");
  wrap.innerHTML="";
  if(!data.sections)return;
  var keys=["Summary","General","Tyre","Brake","Suspension","EngineCooling","Handling"];
  for(var i=0;i<keys.length;i++){
    var s=data.sections[keys[i]];
    if(!s||!s.content)continue;
    var card=document.createElement("div");
    card.className="card"+(s.hasRecommendation?" rec":"");
    var det=document.createElement("details");det.open=true;
    var sum=document.createElement("summary");
    sum.textContent=keys[i];
    if(s.hasRecommendation){var b=document.createElement("span");b.className="badge rec";b.textContent="Rec";sum.appendChild(b);}
    det.appendChild(sum);
    var body=document.createElement("div");body.className="card-body";body.textContent=s.content;
    det.appendChild(body);
    card.appendChild(det);wrap.appendChild(card);
  }
}

/* ---------- C3: tyre temp graphical bands (4 groups x 3 bars) ---------- */
function renderTyres(ls){
  var sec=document.getElementById("tyreSection");
  if(!ls||!ls.tyreTempsAvg||ls.tyreTempsAvg.length!==12){sec.classList.add("hidden");return;}
  sec.classList.remove("hidden");
  var container=document.getElementById("tyreGrid");container.innerHTML="";
  var tyres=["FL","FR","RL","RR"];
  
  for(var t=0;t<4;t++){
    var vIn=ls.tyreTempsAvg[t*3],vMid=ls.tyreTempsAvg[t*3+1],vOut=ls.tyreTempsAvg[t*3+2];
    var group=document.createElement("div");group.className="tyre-group";
    
    var header=document.createElement("div");header.className="tyre-header";
    var press= (ls.tyrePressures && ls.tyrePressures[t]!=null) ? Math.round(ls.tyrePressures[t]*10)/10 + " psi" : "\u2014 psi";
    header.innerHTML = "<div>"+tyres[t]+"</div><div class='tyre-pressure'>"+press+"</div>";
    group.appendChild(header);

    var barsWrap=document.createElement("div");barsWrap.className="tyre-bars";
    var valsArr=[];
    var isLeft = (t % 2 === 0);

    for(var b=0;b<3;b++){
      var v,color;
      // Determine which band we are drawing based on tyre side and loop index
      var bandType = "mid";
      if (isLeft) {
        if (b === 0) bandType = "out"; else if (b === 2) bandType = "in";
      } else {
        if (b === 0) bandType = "in"; else if (b === 2) bandType = "out";
      }

      // Fixed two-tone scheme matching app GUI (TireTempPaintBoxPaint):
      //   hot side  = dark green #008000, cool side = light green #9EE69E
      //   middle    = bright green #00C800 when between inner/outer, orange #FFA500 otherwise
      if(bandType === "in"){
        v=vIn;
        if(v==null)color="#bdc3c7";
        else if(vOut==null||v>=vOut)color="#008000";   // hot side → dark green
        else color="#9EE69E";                           // cool side → light green
      }else if(bandType === "out"){
        v=vOut;
        if(v==null)color="#bdc3c7";
        else if(vIn==null||v>=vIn)color="#008000";     // hot side → dark green
        else color="#9EE69E";                           // cool side → light green
      }else{ // Middle
        v=vMid;
        if(v==null||vIn==null||vOut==null)color="#bdc3c7";   // incomplete data → neutral
        else {
          var middleBetween=(v>vIn&&v<vOut)||(v<vIn&&v>vOut);
          color=middleBetween?"#00C800":"#FFA500";     // green = normal, orange = anomaly
        }
      }
      var bar=document.createElement("div");bar.className="tyre-bar";
      bar.style.background=color;barsWrap.appendChild(bar);
      valsArr.push(v==null?"\u2014":Math.round(v)+"\u00B0");
    }
    group.appendChild(barsWrap);

    var valsDiv=document.createElement("div");valsDiv.className="tyre-vals";
    valsDiv.textContent = valsArr.join(" ");
    group.appendChild(valsDiv);
    container.appendChild(group);
  }
}

function drawBrakeDisc(canvas, avg, max){
  var ctx=canvas.getContext("2d");var W=canvas.width,H=canvas.height;
  var cx=W/2,cy=H/2-10,r=35;
  ctx.clearRect(0,0,W,H);

  var color=brakeColor(max);
  
  // Draw Disc (Rotor)
  ctx.beginPath();
  ctx.arc(cx,cy,r,0,Math.PI*2);
  ctx.fillStyle=color;
  ctx.fill();
  
  // Inner hole for realism
  ctx.beginPath();
  ctx.arc(cx,cy,r*0.3,0,Math.PI*2);
  ctx.fillStyle="#fff";
  ctx.fill();

  // Draw Caliper (simplified shape)
  ctx.beginPath();
  ctx.roundRect(cx+r-5, cy-15, 12, 30, 4);
  ctx.fillStyle=color;
  ctx.fill();
  ctx.strokeStyle="#2c3e50";
  ctx.lineWidth=1;
  ctx.stroke();

  // Text labels
  ctx.fillStyle="#2c3e50";
  ctx.font="bold 14px sans-serif";
  ctx.textAlign="center";
  ctx.fillText(Math.round(avg)+"\u00B0C", cx, cy+r+20);
  ctx.font="11px sans-serif";
  ctx.fillStyle="#7f8c8d";
  ctx.fillText("Max: "+Math.round(max)+"\u00B0C", cx, cy+r+35);
}

function drawTempGauge(canvas, avg, max){
  var ctx=canvas.getContext("2d");var W=canvas.width,H=canvas.height;
  var cx=W/2,cy=H/2-10,r=35;
  ctx.clearRect(0,0,W,H);

  // Background circle (dark charcoal)
  ctx.beginPath();
  ctx.arc(cx,cy,r,0,Math.PI*2);
  ctx.fillStyle="#2c3e50";
  ctx.fill();

  // Temperature Arc (Blue -> Yellow -> Red)
  var startAngle = Math.PI;
  var endAngle = 2 * Math.PI;
  var gradient = ctx.createLinearGradient(cx-r, cy, cx+r, cy);
  gradient.addColorStop(0, "#3498db"); // Blue
  gradient.addColorStop(0.5, "#f1c40f"); // Yellow
  gradient.addColorStop(1, "#e74c3c"); // Red

  ctx.beginPath();
  ctx.arc(cx,cy,r,startAngle,endAngle);
  ctx.strokeStyle = gradient;
  ctx.lineWidth = 8;
  ctx.stroke();

  // Needle for Average value (Range: 60C to 120C)
  var minT = 60, maxT = 120;
  var norm = Math.max(0, Math.min(1, (avg - minT) / (maxT - minT)));
  var needleAngle = startAngle + norm * (endAngle - startAngle);

  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + Math.cos(needleAngle)*r, cy + Math.sin(needleAngle)*r);
  ctx.strokeStyle = "#fff";
  ctx.lineWidth = 3;
  ctx.stroke();

  // Center hub dot
  ctx.beginPath();
  ctx.arc(cx,cy,4,0,Math.PI*2);
  ctx.fillStyle="#fff";
  ctx.fill();

  // Text labels
  ctx.fillStyle="#2c3e50";
  ctx.font="bold 14px sans-serif";
  ctx.textAlign="center";
  ctx.fillText(Math.round(avg)+"\u00B0C", cx, cy+r+20);
  ctx.font="11px sans-serif";
  ctx.fillStyle="#7f8c8d";
  ctx.fillText("Max: "+Math.round(max)+"\u00B0C", cx, cy+r+35);
}

/* ---------- C4: brake temp discs (Canvas) ---------- */
function renderBrakes(ls){
  var sec=document.getElementById("brakeSection");
  if(!ls||!ls.brakeTempAvg||ls.brakeTempAvg.length!==4||!ls.brakeTempMax||ls.brakeTempMax.length!==4){sec.classList.add("hidden");return;}
  sec.classList.remove("hidden");
  var grid=document.getElementById("brakeWrap");grid.innerHTML="";
  var corners=["FL","FR","RL","RR"];
  for(var i=0;i<4;i++){
    (function(idx){
      var box=document.createElement("div");box.className="brake-box";
      var title=document.createElement("div");title.className="brake-title";
      title.textContent=corners[idx]+" Brake";box.appendChild(title);
      var cv=document.createElement("canvas");cv.width=140;cv.height=130;
      box.appendChild(cv);grid.appendChild(box);
      drawBrakeDisc(cv,ls.brakeTempAvg[idx],ls.brakeTempMax[idx]);
    })(i);
  }
}

function renderEngineTemps(ls){
  var sec=document.getElementById("engineSection");
  if(!ls||ls.waterTempAvg===undefined||ls.oilTempAvg===undefined){sec.classList.add("hidden");return;}
  sec.classList.remove("hidden");
  var grid=document.getElementById("engineWrap");grid.innerHTML="";
  
  var temps=[{label:"Water", avg:ls.waterTempAvg, max:ls.waterTempMax}, {label:"Oil", avg:ls.oilTempAvg, max:ls.oilTempMax}];
  for(var i=0;i<temps.length;i++){
    var t=temps[i];
    var box=document.createElement("div");box.className="brake-box";
    var title=document.createElement("div");title.className="brake-title";
    title.textContent=t.label+" Temp";box.appendChild(title);
    var cv=document.createElement("canvas");cv.width=140;cv.height=130;
    box.appendChild(cv);grid.appendChild(box);
    drawTempGauge(cv,t.avg,t.max);
  }
}

/* ---------- C5: damper histograms (canvas per corner, shared Y) ---------- */
function renderDampers(ls){
  var sec=document.getElementById("damperSection");
  if(!ls||!ls.damperHistograms){sec.classList.add("hidden");return;}
  var corners=["LF","RF","LR","RR"];var present=[];
  for(var i=0;i<4;i++)if(ls.damperHistograms[corners[i]])present.push(corners[i]);
  if(present.length===0){sec.classList.add("hidden");return;}
  sec.classList.remove("hidden");

  // Setup toggle buttons
  var btnGrid=document.getElementById("btnDamperGrid");
  var btnComb=document.getElementById("btnDamperCombined");
  var gridWrap=document.getElementById("damperGrid");
  var combWrap=document.getElementById("damperCombinedWrap");

  btnGrid.onclick=function(){
    btnGrid.classList.add("active");btnComb.classList.remove("active");
    gridWrap.classList.remove("hidden");combWrap.classList.add("hidden");
  };
  btnComb.onclick=function(){
    btnComb.classList.add("active");btnGrid.classList.remove("active");
    combWrap.classList.remove("hidden");gridWrap.classList.add("hidden");
  };

  // Threshold config from the API (mirrors app g_thresholds). Fallback defaults keep it safe.
  var cfg=ls.damperConfig||{slowRange:15,yOffsetPct:20,minYPct:20};

  // Normalize each corner's raw counts to fractions — exactly what the app does before drawing
  // (MotecSetupForm.cpp: suspensionData[i].bins[b] = hist.bins[b]/hist.totalCount).
  function normBins(h){var b=h.bins||[];var tot=0;for(var i=0;i<b.length;i++)tot+=b[i];if(tot<=0)return{f:[],count:0};return{f:b.map(function(v){return v/tot;}),count:tot};}
  var norm={};
  for(var p=0;p<present.length;p++){norm[present[p]]=normBins(ls.damperHistograms[present[p]]);}

  // Shared Y max across all present corners = the largest bin FRACTION (mirrors suspensionMaxBin, which starts at 0).
  var ySharedFrac=0;
  for(var p4=0;p4<present.length;p4++){var nf=norm[present[p4]].f;for(var b3=0;b3<nf.length;b3++)if(nf[b3]>ySharedFrac)ySharedFrac=nf[b3];}

  gridWrap.innerHTML="";
  for(var p3=0;p3<present.length;p3++){
    (function(corner){
      var box=document.createElement("div");box.className="damper-box";
      var title=document.createElement("div");title.className="damper-title";
	  title.textContent=corner+" Rebound/bump Velocity";box.appendChild(title);
      var cv=document.createElement("canvas");cv.width=320;cv.height=150;
      box.appendChild(cv);gridWrap.appendChild(box);
      drawHistogram(cv,norm[corner],ySharedFrac,cfg);
    })(present[p3]);
  }

  var combCanvas=document.getElementById("damperCombinedCanvas");
  drawCombinedHistogram(combCanvas,ls.damperHistograms,cfg);
}

function drawHistogram(canvas,nf,ySharedFrac,cfg){
  // Mirrors TFormMotecSetup::DrawModernHistogram (GDI+) exactly: same margins, slow band,
  // gradient bars on a shared Y scale, and % / value axis labels. `nf.f` = normalized bin fractions.
  var ctx=canvas.getContext("2d");var W=canvas.width,H=canvas.height;
  var ml=35,mb=20,cw=W-ml,ch=H-mb;   // app: marginLeft=35, marginBottom=20 (no top/right margin)
  var bins=nf.f||[];if(bins.length===0)return;
  ctx.clearRect(0,0,W,H);

  // Background: deep charcoal (#1a1a1a).
  ctx.fillStyle="#1a1a1a";ctx.fillRect(0,0,W,H);

  var slowRange=cfg.slowRange||0;
  var displayMax=Math.max(ySharedFrac*(1+(cfg.yOffsetPct||0)/100),(cfg.minYPct||0)/100); // app formula

  // Slow-damper band: symmetric around v=0, width from threshold (app uses chartWidth/400 px-per-unit).
  if(slowRange>0){
    var xCenter=ml+cw/2;var halfW=slowRange*(cw/400);
    ctx.fillStyle="rgba(60,60,100,.39)";ctx.fillRect(xCenter-halfW,0,halfW*2,H); // app fills full height
  }

  // Grid: vertical center line (v=0) + horizontal quarter lines.
  ctx.strokeStyle="rgba(60,60,60,.31)";ctx.lineWidth=1;
  var cx=Math.round(ml+cw/2)+0.5;
  ctx.beginPath();ctx.moveTo(cx,0);ctx.lineTo(cx,ch);ctx.stroke();
  for(var i=1;i<4;i++){var gy=Math.round(ch*i/4)+0.5;ctx.beginPath();ctx.moveTo(ml,gy);ctx.lineTo(W,gy);ctx.stroke();}

  // Bars: contiguous (no gap), gradient blue top->bottom, shared Y scale across corners.
  var binW=cw/bins.length,scaleY=ch/displayMax;
  if(ySharedFrac>0){
    var grad=ctx.createLinearGradient(0,0,W,H);grad.addColorStop(0,"#00b4ff");grad.addColorStop(1,"#003c78");
    ctx.fillStyle=grad;
    for(var b2=0;b2<bins.length;b2++){var bh=bins[b2]*scaleY;if(bh<=0)continue;ctx.fillRect(ml+b2*binW,ch-bh,binW,bh);}
  }

  // Y-axis % labels (top..bottom), matching app's displayMax*(1-i/4)*100.
  ctx.fillStyle="#b4b4b4";ctx.font="8px sans-serif";ctx.textAlign="left";
  for(var i=1;i<4;i++){var gy2=(ch*i/4);var val=displayMax*(1-i/4)*100;ctx.fillText(val.toFixed(1)+"%",5,gy2-7);}
  ctx.fillText("0.0%",5,ch-7);

  // X-axis value labels every 50 mm/s (−200..+200), mapped with span 410 like the app.
  ctx.textAlign="center";ctx.font="9px sans-serif";
  for(var t=-200;t<=200;t+=50){var tx=ml+((t+205)/410)*cw;ctx.fillText(t,tx,H-6);}
}

function drawCombinedHistogram(canvas,damperData,cfg){
  cfg=cfg||{slowRange:15,yOffsetPct:20,minYPct:20}; // app g_thresholds via /api/report damperConfig
  var ctx=canvas.getContext("2d");var W=canvas.width,H=canvas.height;
  var slowRange=cfg.slowRange||0;   // dynamic threshold (mm/s), not a fixed constant
  var cornerKeys=["LF","RF","LR","RR"]; 
  var colorMap={LF:"#ed403b", RF:"#5654f4", LR:"#ec4899", RR:"#00beea"};

  ctx.fillStyle="#1e1e22";ctx.fillRect(0,0,W,H);
  var pad={l:60,r:30,t:90,b:30};var pw=W-pad.l-pad.r,ph=H-pad.t-pad.b;

  var totalSamples=0;var validCorners=[];var cornerStats={};
  for(var c=0;c<cornerKeys.length;c++){
    var key=cornerKeys[c];var h=damperData[key];
    if(!h||!h.bins)continue;
    var bins=h.bins;var count=bins.reduce((a,b)=>a+b,0);
    totalSamples+=count;validCorners.push(key);
    
    cornerStats[key]={
      rebHi:h.rebHi||"0", rebLo:h.rebLo||"0", rebAvg:h.rebAvg||"0",
      bmpHi:h.bmpHi||"0", bmpLo:h.bmpLo||"0", bmpAvg:h.bmpAvg||"0",
      bins:bins.map(function(v){return v/count;}),count:count
    };
  }

  // Header
  ctx.fillStyle="#7f8c8d";ctx.font="bold 12px sans-serif";ctx.textAlign="left";
  ctx.fillText(totalSamples+" samples",pad.l,25);
  ctx.textAlign="right";ctx.fillText("Zoom Linked",W-pad.r,25);

  var rowH=18;
  var statsTable = document.getElementById("damperStatsTable");
  statsTable.innerHTML = "<div><b>Corner</b></div><div><b>Rebound (A/F/S)</b></div><div><b>Bump (S/F/A)</b></div>";

  for(var vC=0;vC<validCorners.length;vC++){
    var k=validCorners[vC];var s=cornerStats[k];
    
    // Captions removed from canvas to clean up the histogram view

	var rAvg = s.rebAvg || "0", rHi = s.rebHi || "0", rLo = s.rebLo   || "0";
	var bLo = s.bmpLo   || "0", bHi = s.bmpHi || "0", bAvg = s.bmpAvg || "0";

    var row = document.createElement("div");
    row.style.display = "contents";
    row.innerHTML = '<div><b>' + k + '</b></div><div>' + rAvg + '% / ' + rHi + '% / ' + rLo + '%</div><div>' + bLo + '% / ' + bHi + '% / ' + bAvg + '%</div>';
    statsTable.appendChild(row);
  }

  // Y-axis labels (Dynamic scale based on data + headroom)
  var globalMax = 0;
  for(var cC=0; cC<validCorners.length; cC++){
    var kC=validCorners[cC]; var bC=cornerStats[kC].bins;
    var mB=0; for(var i=0; i<bC.length; i++) if(bC[i]>mB) mB=bC[i];
    if(mB>globalMax) globalMax=mB;
  }

  // Apply App GUI scaling exactly as MotecSetupForm.cpp does, using the live thresholds from damperConfig.
  var displayMax = Math.max(globalMax * (1 + (cfg.yOffsetPct||20)/100), (cfg.minYPct||20) / 100);

  ctx.fillStyle="#7f8c8d"; ctx.font="9px sans-serif"; ctx.textAlign="right";
  var maxPct = displayMax * 100;
  for(var t=0; t<=4; t++){
    var pctVal = maxPct * (t / 4);
    var yPos = pad.t + ph - (t / 4 * ph);
    ctx.fillText(Math.round(pctVal), pad.l-10, yPos+3);
  }

  // Grid & Axes — X ticks at −200..+200 step 50 mm/s, mapped with span 410 (app: chartLeft + ((val+205)/410)*chartWidth).
  ctx.strokeStyle="rgba(60,70,70,0.39)";ctx.lineWidth=1;
  for(var tx=-200;tx<=200;tx+=50){
    var x=(pad.l+((tx+205)/410)*pw)|0;
    ctx.beginPath();ctx.moveTo(x,pad.t);ctx.lineTo(x,pad.t+ph);ctx.stroke();
    ctx.fillStyle="#7f8c8d";ctx.font="9px sans-serif";ctx.textAlign="center";
    ctx.fillText(tx,x,H-10);
  }

  // Slow band — dynamic threshold, (int) truncation + clamp to plot area exactly like the app GDI+ code.
  if(slowRange>0){
    var xLow=pad.l+(Math.trunc(-slowRange)+205)/410*pw;
    var xHigh=pad.l+(Math.trunc(+slowRange)+205)/410*pw;
    if(xLow<pad.l)xLow=pad.l;if(xHigh>pad.l+pw)xHigh=pad.l+pw;
    ctx.fillStyle="rgba(120,140,255,.196)";ctx.fillRect(xLow,pad.t,xHigh-xLow,ph); // app: Color(50,120,140,255)
  }

  // Curves with fill
  for(var cC2=0;cC2<validCorners.length;cC2++){
    var kC2=validCorners[cC2];var sC2=cornerStats[kC2];var colC2=colorMap[kC2]||"#fff";
    
    // App maps each bin by its CENTER: bc = -205 + (b+0.5)*10, then x = chartLeft + ((bc+205)/410)*chartWidth.
    function bx(i){var bc=-205+(i+0.5)*10;return pad.l+((bc+205)/410)*pw;}

    ctx.beginPath();
    ctx.moveTo(pad.l, pad.t+ph);
    for(var i=0;i<sC2.bins.length;i++){
      var x=bx(i)|0;
      var y=pad.t+ph-(sC2.bins[i]/displayMax*ph);
      ctx.lineTo(x,y);
    }
    ctx.lineTo(bx(sC2.bins.length-1)|0, pad.t+ph); // close along baseline to last bin center (app: pts[kBin].X)
    ctx.closePath();

    // Fill area under curve — app uses alpha=50/255 ≈ 0.196 for the translucent fill.
    ctx.globalAlpha=0.196;ctx.fillStyle=colC2;ctx.fill();ctx.globalAlpha=1;

    // Draw line on top (full opacity, width 2 — matches app Gdiplus::Pen(colors[i], 2.0f)).
    ctx.strokeStyle=colC2;ctx.lineWidth=2;
    ctx.beginPath();
    for(var i=0;i<sC2.bins.length;i++){
      var x=bx(i)|0;
      var y=pad.t+ph-(sC2.bins[i]/displayMax*ph);
      if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    }
    ctx.stroke();
  }
}

/* ---------- main render + poll loop (C1) ---------- */
function render(data){
  document.getElementById("emptyNotice").classList.toggle("hidden",!!data.meta&&data.meta.hasData===true);
  if(!data.sections||!Object.keys(data.sections).length)return;
  var ls=data.lapStats||{};
  renderSections(data);renderTyres(ls);renderBrakes(ls);renderEngineTemps(ls);renderDampers(ls);
}

function poll(){
  fetch("/api/report?t=" + Date.now()).then(function(r){return r.json()}).then(function(d){
    if(!d.version)return;
    if(d.version===lastVersion)return; // no change — skip re-render.
    lastVersion=d.version;render(d);
    var now=new Date();document.getElementById("lastUpdated").textContent=now.toLocaleTimeString();
  }).catch(function(){});
}

poll();setInterval(poll,POLL_MS);
</script>
</body>
</html>)XHTMLDOC";
    return page;
}

#endif // TABLETREPORTPAGE_H
