"use strict";
const $=id=>document.getElementById(id), pages=window.SCREENS;
let current=0, noticeTimer;
const routes=[["Home",0],["Games",1],["Multiplayer",8],["Files",7],["Settings",3]];
const details=new Set([4,5,6,9,10,15,16,17,18,19]);
const related={
0:[1,12,3],1:[11,12,21],3:[4,5,6,9,10,16],
7:[13,17,21,11],8:[9,13],10:[21,7],11:[22],15:[4,18,19],
16:[15,18,19,14],18:[19,16],19:[18,16],21:[1,7],22:[11]
};
function message(text){clearTimeout(noticeTimer);$("notice").textContent=text;noticeTimer=setTimeout(()=>$("notice").textContent="",6500);}
function button(label,action,rect){const b=document.createElement("button");b.type="button";b.setAttribute("aria-label",label);b.onclick=action;if(rect){b.className="hotspot";["left","top","width","height"].forEach((p,i)=>b.style[p]=rect[i]+"%");}else b.textContent=label;return b;}
function hit(label,target,rect){$("hotspots").append(button(label,()=>typeof target==="number"?show(target):message(target),rect));}
function show(index,updateHash=true){
 current=(index+pages.length)%pages.length;const p=pages[current];
 $("picker").value=p.id;$("title").textContent=String(current+1).padStart(2,"0")+" · "+p.title;$("note").textContent=p.note;
 $("art").src=p.image;$("art").alt=p.title+" — GameChangersAI OS interface concept. "+p.note;
 $("original").href=p.image;$("hotspots").replaceChildren();$("quicklinks").replaceChildren();$("notice").textContent="";
 if(updateHash)history.replaceState(null,"","#"+p.id);
 document.querySelectorAll(".thumb").forEach((b,i)=>b.setAttribute("aria-current",String(i===current)));
 if(current!==2 && current!==20 && current!==22){
   routes.forEach(([label,to],i)=>hit("Open "+label,to,[0.7,14.4+i*10.0,17.6,8.6]));
   hit("Open Battery",4,[87,1.5,12,8]);hit("Open Storage",10,[69,1.5,17,8]);
 }
 if(details.has(current))hit("Back to Settings",3,[20,9.8,10,7.4]);
 if(current===0){hit("Play Byte Buddy",20,[23,40.8,20.5,9]);[0,1,2].forEach((x)=>hit("View game library",1,[20+x*26,56,25,32.5]));}
 if(current===1)hit("Launch selected game",20,[20,25,25,59]);
 if(current===2)hit("Continue to Home",0,[0,0,100,100]);
 if(current===3){[5,6,9,10,4,16].forEach((n,i)=>hit("Open "+pages[n].title,n,[20+(i%2)*39.5,22+Math.floor(i/2)*22.2,38.5,20]));}
 if(current===4)hit("Refresh battery preview","This is a design concept. No live battery reading was requested.",[59,70,38,9]);
 if(current===7){hit("Open Games folder",1,[21,77,25,9]);hit("Refresh files preview","Sample files shown. The device storage has not changed.",[47,77,25,9]);}
 if(current===11){hit("Review save deletion",22,[59.8,78.5,38.8,9.5]);hit("Open saved game",20,[20,78.5,38.5,9.5]);}
 if(current===16){hit("Open Sensors",15,[20,63,25,25]);hit("Open Diagnostics",18,[46,63,25,25]);hit("Clock sync preview","Clock sync requires the host-assisted firmware service; this preview does not set the clock.",[71,50,27,10]);}
 if(current===20)hit("Return to Home",0,[0,0,100,100]);
 if(current===21){hit("Play selected game",20,[20,80,25,10]);hit("Refresh game library preview","Sample game list shown; no device operation was performed.",[46,80,25,10]);}
 if(current===22){hit("Cancel deletion",11,[28.7,67.5,23.6,10.5]);hit("Preview Delete action","Preview only. Nothing was deleted.",[54.2,67.5,24.2,10.5]);}
 for(const i of related[current]||[])$("quicklinks").append(button(pages[i].title,()=>show(i)));
}
for(const [i,p] of pages.entries()){
 const o=document.createElement("option");o.value=p.id;o.textContent=String(i+1).padStart(2,"0")+" · "+p.title;$("picker").append(o);
 const b=button(p.title,()=>{show(i);$("viewport").scrollIntoView({block:"center"});});b.className="thumb";
 const img=document.createElement("img");img.src=p.image;img.alt="";img.loading="lazy";const span=document.createElement("span");span.textContent=String(i+1).padStart(2,"0")+" · "+p.title;b.replaceChildren(img,span);$("gallery").append(b);
}
$("picker").onchange=()=>show(pages.findIndex(p=>p.id===$("picker").value));
$("prev").onclick=()=>show(current-1);$("next").onclick=()=>show(current+1);
$("size").onclick=()=>{const on=$("stage").classList.toggle("native");$("size").setAttribute("aria-pressed",String(on));$("size").textContent=on?"Fit to window":"1280 × 720 view";};
$("targets").onchange=()=>$("hotspots").classList.toggle("show-targets",$("targets").checked);
window.addEventListener("keydown",e=>{if(["INPUT","SELECT","TEXTAREA"].includes(e.target.tagName)||e.altKey||e.metaKey||e.ctrlKey)return;if(e.key==="ArrowRight"||e.key==="ArrowLeft"){e.preventDefault();show(current+(e.key==="ArrowRight"?1:-1));}if(e.key==="Escape")show(current===22?11:0);});
window.addEventListener("hashchange",()=>show(Math.max(0,pages.findIndex(p=>p.id===location.hash.slice(1))),false));
show(Math.max(0,pages.findIndex(p=>p.id===location.hash.slice(1))),false);
