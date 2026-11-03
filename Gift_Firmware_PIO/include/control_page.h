#pragma once
#include <Arduino.h>

static const char ControlPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#ff962f"><title>晗歌 · はづき</title>
<style>
:root{color-scheme:light;font:16px/1.6 system-ui,sans-serif;color:#36271e;background:#fff8ef}
*{box-sizing:border-box}body{max-width:620px;margin:auto;padding:28px 18px 60px}h1{margin:0;font-size:30px}h2{font-size:19px;margin:0 0 14px}p{margin:8px 0}small,.muted{color:#796454}header{padding:12px 4px 24px}.jp{letter-spacing:.2em;color:#a95816}
section{background:white;border:1px solid #f1dec8;border-radius:20px;padding:20px;margin-bottom:16px}
button,input,select{font:inherit}button{border:0;border-radius:12px;background:#ff962f;color:#342013;padding:11px 16px;cursor:pointer;min-height:46px}button.secondary{background:#fff0dc}button.danger{background:#fff2f0;color:#a33424}button:disabled{opacity:.45;cursor:wait}
.row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}label{display:block;margin:15px 0 5px}input[type=range]{width:100%;accent-color:#e57917}input[type=file]{max-width:100%;margin:12px 0}select{width:100%;padding:9px;border:1px solid #e9d6c0;border-radius:10px;background:#fff}
.movie{padding:12px 0;border-top:1px solid #f4e9dc;display:flex;align-items:center;gap:8px}.movie span{flex:1;overflow-wrap:anywhere}.movie button{padding:8px 12px;font-size:14px}progress{width:100%;accent-color:#f19339}#notice{position:sticky;bottom:12px;border-radius:12px;background:#392c24;color:#fff;padding:12px;display:none}#state{font-weight:600;color:#a95510}
</style>
<header><h1>晗歌</h1><div class="jp">はづき</div><p class="muted">让这束橙光，陪你度过每一天。</p></header>
<section><h2>正在展示</h2><p id="state" role="status">正在连接小舞台…</p><p id="detail" class="muted"></p><div class="row"><button id="pause">暂停</button><button id="next" class="secondary">下一段</button><button id="rescan" class="secondary">重新读取素材</button></div></section>
<section><h2>我的视频</h2><div id="movies"></div><p class="muted">选择随礼提供的专用视频文件（.mjpeg），即可添加。普通 MP4 需要由制作方先转换。</p><input id="file" type="file" accept=".mjpeg"><button id="upload">添加视频</button><progress id="progress" max="100" value="0" hidden></progress><small>上传期间暂时显示备用画面。请保持连接并等到“添加完成”，期间不要拔电或取卡。</small></section>
<section><h2>舒适地摆在桌边</h2><form id="settings">
<label for="brightness">屏幕亮度 <output id="brightnessValue"></output>%</label><input id="brightness" type="range" min="10" max="100" value="55">
<label for="led">橙色灯光 <output id="ledValue"></output>%</label><input id="led" type="range" min="0" max="40" value="8">
<details><summary>画面设置</summary><label for="fps">播放速度</label><select id="fps"><option value="8">8 帧／秒</option><option value="10">10 帧／秒</option><option value="12" selected>12 帧／秒（默认素材）</option><option value="15">15 帧／秒</option><option value="20">20 帧／秒</option></select><small>请与素材转换时的帧率保持一致。</small>
<label for="rotation">安装方向</label><select id="rotation"><option value="4">棱镜展示（默认）</option><option value="0">直接看屏幕</option><option value="1">旋转 90°</option><option value="2">旋转 180°</option><option value="3">旋转 270°</option><option value="5">侧面棱镜</option></select></details>
<p><button type="submit">保存设置</button></p></form></section>
<p class="muted">无需安装 App，也不需要家里的 Wi-Fi。连接小舞台的 Wi-Fi 后，手机提示“没有互联网”是正常的，请选择保持连接。网址：http://192.168.4.1</p>
<div id="notice" role="status" aria-live="polite"></div>
<script>
const $=id=>document.getElementById(id);let busy=false,initialized=false,paused=false,listKey='',noticeTimer;
function notice(message){$('notice').textContent=message;$('notice').style.display='block';clearTimeout(noticeTimer);noticeTimer=setTimeout(()=>$('notice').style.display='none',6000);}
async function api(path,data){const options=data?{method:'POST',headers:{'X-Hazuki-Request':'1','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)}:{};const response=await fetch(path,options);const json=await response.json();if(!response.ok)throw Error(json.error||'没有完成，请重试');return json;}
async function refresh(){if(busy)return;try{const s=await api('/api/state');paused=s.paused;$('pause').textContent=paused?'继续播放':'暂停';$('state').textContent=s.uploading?'正在添加素材':s.current?(paused?'已暂停':'正在播放')+' · '+s.current:'橙光待机画面';$('detail').textContent=s.error||(!s.sd?'没有读到内存卡；备用画面仍可正常展示。':'所有视频都会按文件名顺序循环播放。');
if(!initialized){for(const key of ['brightness','led','fps','rotation'])$(key).value=s[key];updateLabels();initialized=true;}
const key=JSON.stringify(s.movies);if(key!==listKey){listKey=key;$('movies').replaceChildren();if(!s.movies.length){const p=document.createElement('p');p.textContent='还没有视频，先欣赏橙光画面吧。';$('movies').append(p);}for(const name of s.movies){const row=document.createElement('div');row.className='movie';const title=document.createElement('span');title.textContent=name;const play=document.createElement('button');play.textContent='播放';play.onclick=()=>command('/api/play',{name});const del=document.createElement('button');del.textContent='移除';del.className='danger';del.onclick=()=>{if(confirm('移除这段视频？\n'+name))command('/api/delete',{name});};row.append(title,play,del);$('movies').append(row);}}
}catch(e){$('state').textContent='暂时没有连上小舞台';$('detail').textContent='请确认仍连接着小舞台的 Wi-Fi，再稍等片刻。';}}
async function command(path,data={}){if(busy)return;busy=true;try{await api(path,data);}catch(e){notice(e.message);}finally{busy=false;refresh();}}
function updateLabels(){for(const key of ['brightness','led'])$(key+'Value').textContent=$(key).value;}
for(const key of ['brightness','led'])$(key).oninput=updateLabels;
$('pause').onclick=()=>command('/api/pause',{paused:paused?'0':'1'});$('next').onclick=()=>command('/api/next');$('rescan').onclick=()=>command('/api/rescan');
$('settings').onsubmit=async event=>{event.preventDefault();if(busy)return;busy=true;try{const data={};for(const key of ['brightness','led','fps','rotation'])data[key]=$(key).value;await api('/api/settings',data);notice('设置已保存');}catch(e){notice(e.message);}finally{busy=false;refresh();}};
$('upload').onclick=()=>{if(busy)return;const file=$('file').files[0];if(!file)return notice('先选择一段视频');if(!/^[A-Za-z0-9_-]{1,42}\.mjpeg$/.test(file.name))return notice('文件名请使用英文字母、数字、下划线或短横线，扩展名为 .mjpeg');if(file.size>128*1024*1024)return notice('每段视频最多 128 MiB，请先分段');busy=true;document.querySelectorAll('button').forEach(b=>b.disabled=true);$('progress').hidden=false;$('progress').value=0;const xhr=new XMLHttpRequest();xhr.open('POST','/api/upload');xhr.setRequestHeader('X-Hazuki-Request','1');xhr.timeout=600000;xhr.upload.onprogress=e=>{if(e.lengthComputable)$('progress').value=e.loaded/e.total*100;};const finish=message=>{busy=false;document.querySelectorAll('button').forEach(b=>b.disabled=false);$('progress').hidden=true;notice(message);refresh();};xhr.onload=()=>{let r={};try{r=JSON.parse(xhr.responseText);}catch(e){}finish(xhr.status===200?'添加完成':r.error||'添加失败，请重试');};xhr.onerror=()=>finish('连接中断，请重新连接后重试');xhr.ontimeout=()=>finish('传输超时，请重试');const data=new FormData();data.append('file',file);xhr.send(data);};
refresh();setInterval(refresh,3000);
</script></html>)HTML";
