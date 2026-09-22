'use strict';
const $ = id => document.getElementById(id);
let kind='appearance', preset=true, page=0, pages=1, folder='', source='user', revision=-1;
let queryEpoch=0, folderEpoch=0, detailEpoch=0, folderOffset=0, diagOffset=0, selected='';
let initialized=false, pendingQuery=null, queryTimer, folderTimer;
let thumbEpoch=0, activeThumbs=0, thumbQueue=[], thumbControllers=new Set(), objectUrls=new Set();
const observer = new IntersectionObserver(entries => {
  for (const entry of entries) {
    if (entry.isIntersecting && !entry.target.dataset.queued) {
      entry.target.dataset.queued='1';
      thumbQueue.push({node:entry.target,id:entry.target.dataset.id,epoch:thumbEpoch});
    }
  }
  pumpThumbs();
}, {root:$('viewport'), rootMargin:'0px', threshold:0.01});

function el(tag, cls, text) { const e=document.createElement(tag); if(cls)e.className=cls; if(text!==undefined)e.textContent=text; return e; }
function error(message) { $('error').textContent=message||''; $('error').hidden=!message; }
async function api(route, args={}, post=false, signal) {
  const url='api/'+route+(post?'':'?'+new URLSearchParams(args));
  const response=await fetch(url, post?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(args),signal}:{signal});
  const data=await response.json(); if(!response.ok)throw new Error(data.error||'请求失败'); return data;
}
async function action(route,args={}) {try{error('');await api(route,args,true);await poll();}catch(e){error(e.message);}}
function resetThumbs() {
  thumbEpoch++; observer.disconnect(); thumbQueue=[];
  for(const c of thumbControllers)c.abort();
  for(const u of objectUrls)URL.revokeObjectURL(u);
  objectUrls.clear();
}
async function smallImage(blob) {
  const url=URL.createObjectURL(blob);
  try {
    const image=new Image(); image.src=url; await image.decode();
    const canvas=document.createElement('canvas');
    const scale=Math.min(1,300/Math.max(image.naturalWidth,image.naturalHeight));
    canvas.width=Math.max(1,Math.round(image.naturalWidth*scale));canvas.height=Math.max(1,Math.round(image.naturalHeight*scale));
    canvas.getContext('2d').drawImage(image,0,0,canvas.width,canvas.height);
    return await new Promise(resolve=>canvas.toBlob(resolve,'image/jpeg',0.85));
  } finally { URL.revokeObjectURL(url); }
}
function pumpThumbs() {
  while(activeThumbs<2 && thumbQueue.length) {
    const job=thumbQueue.shift();
    if(job.epoch!==thumbEpoch||!job.node.isConnected)continue;
    const rect=job.node.getBoundingClientRect(), view=$('viewport').getBoundingClientRect();
    if(rect.bottom<=view.top||rect.top>=view.bottom){delete job.node.dataset.queued;continue;}
    activeThumbs++;
    const controller=new AbortController();thumbControllers.add(controller);
    (async()=>{
      try {
        const r=await fetch('api/thumb?id='+encodeURIComponent(job.id),{signal:controller.signal});
        if(!r.ok)throw new Error((await r.json()).error);
        const blob=await smallImage(await r.blob());
        if(job.epoch!==thumbEpoch||!job.node.isConnected||!blob)return;
        const url=URL.createObjectURL(blob);objectUrls.add(url);
        const img=el('img');img.alt=job.node.dataset.name;img.src=url;
        job.node.querySelector('.placeholder').replaceWith(img);
      }catch(e){if(e.name!=='AbortError'&&job.epoch===thumbEpoch){const label=job.node.querySelector('.placeholder small');if(label)label.textContent='缩略图不可用';job.node.title=e.message;}}
      finally{activeThumbs--;thumbControllers.delete(controller);pumpThumbs();}
    })();
  }
}
function tabButton(tab) {
  const b=el('button',tab[0]===kind?'active':'');b.dataset.kind=tab[0];b.setAttribute('role','tab');b.setAttribute('aria-selected',tab[0]===kind);
  b.append(el('span','',tab[1]),el('span','count','0'));
  b.onclick=()=>{
    kind=tab[0];preset=tab[2];page=0;folder='';source=preset?'user':'';folderOffset=0;selected='';detailEpoch++;
    $('detailbody').replaceChildren(el('p','muted','选择一个条目，查看来源定位和描述数据。'));
    $('source').value=source;$('sidebar').hidden=!preset;$('foldersearch').value='';
    for(const child of $('tabs').children){child.classList.toggle('active',child===b);child.setAttribute('aria-selected',child===b);}
    updateScope();loadFolders();loadQuery();
  }; return b;
}
function updateScope(){
  $('user').classList.toggle('active',source==='user'&&!folder);
  $('all').classList.toggle('active',!source&&!folder);
  $('scope').textContent=folder||(source==='user'?'用户保存':source==='package'?'VAR 包':'全部来源');
}
async function loadFolders(){
  if(!preset)return;
  const epoch=++folderEpoch;
  try{
    const data=await api('folders',{kind,search:$('foldersearch').value,offset:folderOffset});
    if(epoch!==folderEpoch)return;
    $('folders').replaceChildren();
    for(const row of data.items){
      const b=el('button',row.folder===folder?'active':'');
      const cut=row.folder.indexOf(' / ');b.append(el('span','',row.folder.slice(0,cut)),el('small','',row.folder.slice(cut+3)+' · '+row.count));
      b.title=row.folder;b.onclick=()=>{folder=row.folder;source='';$('source').value='';page=0;updateScope();loadFolders();loadQuery();};$('folders').append(b);
    }
    $('folderprev').disabled=folderOffset===0;$('foldernext').disabled=!data.more;
  }catch(e){error(e.message);}
}
async function loadQuery(){
  const epoch=++queryEpoch;
  if(pendingQuery)pendingQuery.abort();pendingQuery=new AbortController();
  resetThumbs();$('grid').replaceChildren();$('empty').hidden=true;$('results').textContent='正在查询索引…';
  try{
    const data=await api('query',{kind,page,folder,source,sort:$('sort').value,search:$('search').value,author:$('author').value,tag:$('tag').value},false,pendingQuery.signal);
    if(epoch!==queryEpoch)return;
    page=data.page;pages=Math.max(1,Math.ceil(data.total/data.page_size));
    $('results').textContent=`${data.total.toLocaleString()} 个资源`;$('page').textContent=`${page+1} / ${pages}`;
    $('jump').value=page+1;$('jump').max=pages;$('prev').disabled=page===0;$('next').disabled=page+1>=pages;
    $('viewport').scrollTop=0;
    for(const item of data.items){
      const card=el('button','card'+(selected===item.id?' selected':''));card.title=item.name+'\n'+item.source+'\n'+item.path;card.dataset.id=item.id;
      const preview=el('div','preview');preview.dataset.id=item.id;preview.dataset.name=item.name;
      const placeholder=el('div','placeholder',item.name);placeholder.append(el('small','',item.thumb?'等待可见时加载':'无缩略图'));
      preview.append(placeholder,el('span','badge',item.package?'VAR':'用户'));
      const info=el('div','cardinfo');info.append(el('div','cardname',item.name),el('div','cardmeta',item.author||'作者未标注'),el('div','cardmeta',item.package||'松散资源'));
      if(item.diagnostic)info.append(el('div','cardmeta warning','有诊断信息'));
      card.append(preview,info);card.onclick=()=>select(item.id);$('grid').append(card);
      if(item.thumb)observer.observe(preview);
    }
    $('empty').hidden=data.total>0;
  }catch(e){if(e.name!=='AbortError'){error(e.message);$('results').textContent='查询失败';}}
}
async function select(id){
  selected=id;const epoch=++detailEpoch;
  for(const card of $('grid').children)card.classList.toggle('selected',card.dataset.id===id);
  $('detailbody').replaceChildren(el('p','muted','正在读取描述数据…'));
  try{
    const d=await api('detail',{id});if(epoch!==detailEpoch)return;
    const body=$('detailbody');body.replaceChildren(el('h2','',d.name));const dl=el('dl');
    const planButton=el('button','primary','生成导入计划');planButton.id='generateplan';planButton.onclick=()=>startPlan([id]);body.append(planButton);
    const fields=[['作者',d.author||'未标注'],['包',d.package||'松散 / 用户保存'],['标签',d.tags||'来源未提供'],['大小',(d.size/1024).toFixed(1)+' KiB'],['创建 / 条目时间',d.created?new Date(d.created*1000).toLocaleString():'未知'],['时间说明',d.time_note],['来源定位',d.location],['稳定标识',d.id]];
    for(const [label,value]of fields)dl.append(el('dt','',label),el('dd','',value));body.append(dl);
    if(d.diagnostic)body.append(el('p','warning',d.diagnostic));
    if(d.json){const details=el('details');details.append(el('summary','',d.truncated?'描述 JSON（显示前 64 KiB）':'描述 JSON'),el('pre','',d.json));body.append(details);}
  }catch(e){if(epoch===detailEpoch)$('detailbody').replaceChildren(el('p','warning',e.message));}
}
async function poll(){
  try{
    const s=await api('state');
    if(!initialized){
      initialized=true;$('root').value=s.settings.root;$('auto').checked=s.settings.auto;$('source').value=source;
      $('tabs').setAttribute('role','tablist');for(const tab of s.tabs)$('tabs').append(tabButton(tab));
      updateScope();loadQuery();loadFolders();
    }
    for(const tab of $('tabs').children)tab.querySelector('.count').textContent=(s.counts[tab.dataset.kind]||0).toLocaleString();
    $('diagcount').textContent=s.diagnostics;
    $('status').textContent=s.running?`${s.done} / ${s.total||'…'} · ${s.phase}`:`${s.phase} · ${s.sources.toLocaleString()} 个来源`;
    $('status').title=s.phase+'\n索引：'+s.index_path+'\n扫描并发 1；请求并发 4；缩略图并发 2；磁盘缓存 256 MiB / 1024 文件。';
    $('scan').disabled=s.running;$('rebuild').disabled=s.running;$('apply').disabled=s.running;$('auto').disabled=s.running;$('cancel').disabled=!s.running;
    $('progress').max=Math.max(1,s.total);$('progress').value=s.done;
    if(s.error)error(s.error);
    if(revision!==s.revision){if(revision!==-1){loadQuery();loadFolders();}revision=s.revision;}
  }catch(e){error('索引服务连接失败：'+e.message);}
}
async function loadDiagnostics(){
  try{const d=await api('diagnostics',{offset:diagOffset});$('diagitems').replaceChildren();for(const r of d.items){const row=el('div','diagrow');row.append(el('strong','',r.message),el('div','',r.source+'\n'+r.path));$('diagitems').append(row);}
    if(!d.items.length)$('diagitems').append(el('p','muted','没有诊断记录。'));
    $('diagprev').disabled=diagOffset===0;$('diagnext').disabled=d.items.length<100;$('diagpage').textContent=`第 ${diagOffset/100+1} 页`;
  }catch(e){error(e.message);}
}
function debounceQuery(){clearTimeout(queryTimer);queryTimer=setTimeout(()=>{page=0;loadQuery();},250);}
for(const id of ['search','author','tag'])$(id).oninput=debounceQuery;
$('sort').onchange=()=>{page=0;loadQuery();};
$('source').onchange=()=>{source=$('source').value;folder='';page=0;updateScope();loadFolders();loadQuery();};
$('foldersearch').oninput=()=>{clearTimeout(folderTimer);folderTimer=setTimeout(()=>{folderOffset=0;loadFolders();},250);};
$('folderprev').onclick=()=>{folderOffset=Math.max(0,folderOffset-100);loadFolders();};
$('foldernext').onclick=()=>{folderOffset+=100;loadFolders();};
for(const id of ['user','all'])$(id).onclick=()=>{folder='';source=id==='user'?'user':'';$('source').value=source;page=0;updateScope();loadFolders();loadQuery();};
$('prev').onclick=()=>{page=Math.max(0,page-1);loadQuery();};$('next').onclick=()=>{page=Math.min(pages-1,page+1);loadQuery();};
$('go').onclick=()=>{page=Math.max(0,Math.min(pages-1,Number($('jump').value)-1||0));loadQuery();};
$('jump').onkeydown=e=>{if(e.key==='Enter')$('go').click();};
$('clear').onclick=()=>{for(const id of ['search','author','tag'])$(id).value='';folder='';source=preset?'user':'';$('source').value=source;page=0;updateScope();loadFolders();loadQuery();};
$('apply').onclick=()=>action('config',{root:$('root').value,auto:$('auto').checked});
$('root').onkeydown=e=>{if(e.key==='Enter')$('apply').click();};
$('auto').onchange=()=>action('config',{root:$('root').value,auto:$('auto').checked});
$('scan').onclick=()=>action('scan');$('rebuild').onclick=()=>action('scan',{force:true});$('cancel').onclick=()=>action('cancel');
$('diagnostics').onclick=()=>{diagOffset=0;$('diagmodal').hidden=false;$('diagclose').focus();loadDiagnostics();};
$('diagclose').onclick=()=>{$('diagmodal').hidden=true;$('diagnostics').focus();};
$('diagprev').onclick=()=>{diagOffset=Math.max(0,diagOffset-100);loadDiagnostics();};$('diagnext').onclick=()=>{diagOffset+=100;loadDiagnostics();};
document.onkeydown=e=>{const modal=!$('planmodal').hidden?$('planmodal'):!$('diagmodal').hidden?$('diagmodal'):null;if(e.key==='Escape'&&modal)$(modal.id==='planmodal'?'planclose':'diagclose').click();if(e.key==='Tab'&&modal){const buttons=Array.from(modal.querySelectorAll('button:not(:disabled)'));const first=buttons[0],last=buttons[buttons.length-1];if(e.shiftKey&&document.activeElement===first){e.preventDefault();last.focus();}else if(!e.shiftKey&&document.activeElement===last){e.preventDefault();first.focus();}}};
window.setVamRoot=root=>{$('root').value=root;$('apply').click();};
async function pollLoop(){await poll();setTimeout(pollLoop,1500);}pollLoop();

const actionNames={create:'创建',reuse:'复用',update:'更新',missing:'缺失',unsupported:'不支持'};
let planId='',planFilter='',planOffset=0,planSelection=[],planPollTimer=null,planResultEpoch=0;
async function startPlan(ids,locked=''){
  $('planmodal').hidden=false;$('planclose').focus();
  try{
    await api('plan/start',{ids,locked_plan:locked},true);
    planSelection=ids;planId='';planOffset=0;planFilter='';planResultEpoch++;
    $('planitems').replaceChildren();$('planactions').replaceChildren();$('planfile').textContent='';
    $('planlocktext').textContent='';$('plandeclarations').textContent='';$('planreplay').disabled=true;
    clearTimeout(planPollTimer);pollPlan();
  }catch(e){$('planstatus').textContent=e.message;}
}
async function pollPlan(){
  try{
    const s=await api('plan/state');
    $('planstatus').textContent=s.running?`已解析 ${s.done} 个条目 · ${s.phase}`:(s.error||s.phase);
    $('plancancel').disabled=!s.running;
    $('planhistory').disabled=s.running;
    if(s.running){planPollTimer=setTimeout(pollPlan,700);return;}
    if(s.plan_id){planId=s.plan_id;await loadPlanHistory();await loadPlan();}
  }catch(e){$('planstatus').textContent=e.message;}
}
async function loadPlan(){
  if(!planId)return;const epoch=++planResultEpoch;$('planreplay').disabled=true;$('decode').disabled=true;
  try{
    const p=await api('plan/result',{id:planId,action:planFilter,offset:planOffset});if(epoch!==planResultEpoch)return;
    $('planstatus').textContent=(p.status==='ready'?'计划就绪':p.status==='cancelled'?'生成已取消':'计划含缺失 / 不支持项，可继续部分解码')+` · ${p.cycles.length} 个循环 · ${p.inactive_count} 条未启用引用已保留`;
    $('planfile').textContent='已保存：'+p.file;$('planfile').title='完整原始参数、未解释字段、依赖边和锁定版本均在此 JSON 中。';
    $('planreplay').disabled=false;$('decode').disabled=p.status==='cancelled';planSelection=p.selection;
    $('planactions').replaceChildren();
    const all=el('button',planFilter===''?'active':'','全部');all.onclick=()=>{planFilter='';planOffset=0;loadPlan();};$('planactions').append(all);
    for(const [key,label]of Object.entries(actionNames)){const b=el('button',planFilter===key?'active':'',`${label} ${p.counts[key]}`);b.onclick=()=>{planFilter=key;planOffset=0;loadPlan();};$('planactions').append(b);}
    $('planlocktext').textContent=p.version_locks.map(x=>`${x.requested} → ${x.resolved}\n  ${x.source}`).join('\n')||'没有动态或跨包版本引用。';
    $('plandeclarations').textContent=p.declared_dependencies.map(x=>`${x.used?'实际引用':'仅声明'} · ${x.requested}\n  声明来源：${x.declared_by}\n  ${x.used_sources.join(', ')||(x.used?'目标未解析，见缺失 / 不支持项':x.availability==='installed'?'已安装，未被引用':'未安装；未使用声明不阻断')}`).join('\n\n')||'没有包声明依赖。';
    $('planitems').replaceChildren();
    for(const item of p.items){
      const row=el('div','planrow');row.append(el('strong',item.action==='missing'||item.action==='unsupported'?'warning':'',`${actionNames[item.action]} · ${item.path}`));
      row.append(el('div','muted',item.source||'松散资源 / 未解析目标'));
      if(item.reason)row.append(el('p','',item.reason));
      if(item.builtin_mapping){const m=item.builtin_mapping;const detail=el('details');detail.append(el('summary','',`内置映射 · ${m.entry.role} · ${m.entry.gender}`));detail.append(el('pre','',`操作：${m.entry.operation}\n定位：${JSON.stringify(m.entry.locator,null,2)}\n来源文件：\n${Object.values(m.files).map(f=>`${f.path}\nSHA-256 ${f.sha256}`).join('\n')}`));row.append(detail);}
      if(item.reuse_of)row.append(el('div','muted','复用计划内内容：'+item.reuse_of));
      if(item.sha256)row.append(el('div','hash','SHA-256 '+item.sha256));
      if(item.references.length){const refs=el('details');refs.append(el('summary','',`引用来源 ${item.references.length} 处`));for(const r of item.references.slice(0,30))refs.append(el('pre','',`${r.source_location}\n字段：${r.field}\n目标：${r.reference}`));if(item.references.length>30)refs.append(el('p','muted','完整引用见已保存的计划 JSON。'));row.append(refs);}
      $('planitems').append(row);
    }
    if(!p.items.length)$('planitems').append(el('p','muted','此分类没有条目。'));
    $('planpage').textContent=`${p.total} 项 · 第 ${Math.floor(planOffset/100)+1} 页`;$('planprev').disabled=planOffset===0;$('plannext').disabled=planOffset+100>=p.total;
  }catch(e){$('planstatus').textContent=e.message;}
}
$('planclose').onclick=()=>{$('planmodal').hidden=true;const b=$('generateplan');if(b)b.focus();};
$('plancancel').onclick=()=>api('plan/cancel',{},true).catch(e=>{$('planstatus').textContent=e.message;});
$('planreplay').onclick=()=>startPlan(planSelection,planId);
$('planprev').onclick=()=>{planOffset=Math.max(0,planOffset-100);loadPlan();};$('plannext').onclick=()=>{planOffset+=100;loadPlan();};
async function loadPlanHistory(){
  const h=await api('plan/history');$('planhistory').replaceChildren(el('option','','选择已保存计划…'));$('planhistory').firstChild.value='';
  for(const item of h.items){const option=el('option','',`${new Date(item.modified*1000).toLocaleString()} · ${item.id.slice(0,16)} · ${(item.bytes/1024).toFixed(0)} KiB`);option.value=item.id;$('planhistory').append(option);}
  if(planId)$('planhistory').value=planId;
}
$('openplans').onclick=async()=>{$('planmodal').hidden=false;$('planclose').focus();try{await loadPlanHistory();const s=await api('plan/state');if(s.running){clearTimeout(planPollTimer);pollPlan();}else{$('plancancel').disabled=true;if(!planId&&$('planhistory').options.length>1){planId=$('planhistory').options[1].value;$('planhistory').value=planId;}await loadPlan();}}catch(e){$('planstatus').textContent=e.message;}};
$('planhistory').onchange=()=>{if($('planhistory').value){planId=$('planhistory').value;planFilter='';planOffset=0;loadPlan();}};
