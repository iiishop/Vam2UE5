"""Attach actual Editor/test evidence and preserve first-round findings."""
import argparse,json,re,shutil,gzip,hashlib
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('editor',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
root=Path('Saved/LegMuscleAttachments');checks=read(a.editor/'validation.json');assert len(checks)==4 and all(r['valid'] for r in checks)
unit=(root/'unit-2.log').read_text(errors='replace');tests=re.findall(r'Test Completed\. Result=\{(\w+)\} Name=\{([^}]+)\}',unit);assert len(tests)==4 and all(s=='Success' for s,n in tests)
assert 'Result: Succeeded' in (root/'compile-3.log').read_text(errors='replace')
summary=dict(editor=checks,pie=read(a.run/'validation.json'),unit_tests=tests,build='compile-3.log: Succeeded',scope='Normal BP_VamCharacter in real UE Editor/PIE, fixed-step motion capture. No real-time performance or full anatomical validation claim.')
for f in ['compile.log','compile-2.log','compile-3.log','unit-2.log']:shutil.copy2(root/f,a.out/f)
shutil.copy2(a.editor/'identity.json',a.out/'editor-identity.json')
for g in ['M','R','S','C']:
 with gzip.open(a.out/(g+'-runtime.log.gz'),'wb') as f:f.write((a.run/g/'runtime.log').read_bytes())
with gzip.open(a.out/'editor-runtime.log.gz','wb') as f:f.write((a.editor/'all/runtime.log').read_bytes())
editor_cards=[]
for g in ['C','M','S','R']:
 src=a.editor/'all'/(g+'-seated-stop');dst=a.out/('Editor-'+g);dst.mkdir(exist_ok=True)
 shutil.copy2(src/'state.json',dst/'state.json');state=read(src/'state.json');assert state['world_type']=='Editor'
 for view in ['side','rear','oblique']:
  for mode in ['material','gray','world-normal']:
   f=view+'-'+mode+'.png';im=Image.open(src/f);im.save(dst/f,optimize=True);assert im.convert('RGB').tobytes()==Image.open(dst/f).convert('RGB').tobytes()
 editor_cards.append(f'<article><h3>Editor · {g}</h3><img loading="lazy" src="{dst.name}/oblique-material.png"><a href="{dst.name}/state.json">实际配置</a></article>')
paths=read(a.run/'muscle-paths.json');ranges=[]
for action in ['flexion','knee-flexion','seated-stop']:
 for side in ['L','R']:
  for name in ['RectusFemoris','Hamstrings','VastusLateral','VastiCore','Adductors']:
   rows=[r for r in paths if r['action']==action and r['segment']==side+'_Thigh' and r['bone'].endswith('_'+name)]
   ranges.append(dict(action=action,side=side,muscle=name,min_ratio=min(r['ratio'] for r in rows),max_ratio=max(r['ratio'] for r in rows),limited_frames=sum(r['limited'] for r in rows)))
summary['muscle_ranges']=ranges
first=read(root/'pie-1/M/M-knee-flexion/video.json')['samples'];first_ranges=[]
for name in ['RectusFemoris','Hamstrings','VastusLateral']:
 vals=[n['muscle_path']['ratio'] for f in first for s in f['leg_pipeline'] if s['name']=='L_Thigh' for n in s['nodes'] if n['bone'].endswith('_'+name)]
 first_ranges.append(dict(muscle=name,min_ratio=min(vals),max_ratio=max(vals)))
summary['first_round_knee_left']=first_ranges
metrics=read(a.run/'analysis.json');m=[r for r in metrics['surfaces'] if r['group']=='M']
summary['M_sampled_geometry']=dict(snapshot_count=len(m),source_pairs_per_snapshot=m[0]['seam_pairs'],max_seam_cm=max(r['seam_max_cm'] for r in m),new_sharp_flags=sum(r['new_sharp_edges'] for r in m),opposed_normals=sum(r['opposed_normals_vs_R'] for r in m),max_change_vs_R_cm=max(r['max_vs_R_cm'] for r in m))
(a.out/'validation-summary.json').write_text(json.dumps(summary,indent=2))
page=a.out/'report.html';s=page.read_text(encoding='utf-8')
v=summary['M_sampled_geometry']
verdict=f'''<section style="background:white;padding:18px;border-left:5px solid #357f6c"><h2>本轮结果</h2><p><b>独立前后肌群附着已接入正常角色运行链路。</b>编译、4 项 UE 自动测试、4 组 Editor 检查与 12 段 PIE 对照均通过执行及配置检查。M 是继续优化 S 的候选，原 Jiggle 幅度不变。</p><p>动作中取样 {v['snapshot_count']} 帧，每帧 {v['source_pairs_per_snapshot']} 对同源重复点，最大间距 {v['max_seam_cm']:.6f} cm；新增硬边标记 {v['new_sharp_flags']}，相对 R 面法线反向标记 {v['opposed_normals']}。这是有限取样的几何检查，不等于所有动作、所有光照下均已证明没有接缝。</p><p>M 对 R 的最大表面变化 {v['max_change_vs_R_cm']:.3f} cm。该值是形变量，不是真实性评分。下面优先看左 C / 右 M 同步视频；旧 S 和无接触 R 可展开查看。</p><p>第一轮发现外侧股肌路径随屈膝错误缩短，第二轮修正了前侧肌腱导向并重录 M。C/R/S 控制录像保留原身份。新算子尚未自动替换默认配置。</p></section>'''
s=s.replace('<div id="verdict"></div>',verdict)
review='''<section id="visual-review"><h2>实际画面判断</h2><p>抽查同条件连续帧，M 保留了接近 C 的整体轮廓，主要改变屈伸时的肌腹厚度；没有看到新增的明显裂口。当前证据支持结构与形变已经生效，尚不足以断言比 C 更符合真实人体。</p><p>坐姿急停的座面遮住部分臀腿下缘，不能据此确认被遮挡区域没有接缝。两段无遮挡屈伸视频、灰模及 World Normal 图用于补充检查。历史接触拒绝单独记录，没有关闭安全检查来获得“成功”。</p><p><a href="media-validation.json">视频校验</a>：15 个视频均有 210 帧；另抽查 24 个解码帧，与原始 UE 截图比较（排除状态文字）。原始 PNG 在 IEND 后存在多余尾字节，编码器会报警，所验解码帧与原图仍一致到有损编码精度。不是用生成图代替实跑截图。</p><p>实现提交：a630ba5（独立附着），567e270（前侧导向修正）。候选入口：正常 Body 的 LegChainEnabled=true、LegChainMethod=4；本次对比关闭候选的接触，保留 Glute/Leg Jiggle，参数与 C 一致。原生产默认没有自动改变。</p></section>'''
s=s.replace('<h2>结构改动与边界</h2>',review+'<h2>结构改动与边界</h2>')
sections='<h2>连续运动帧复核</h2><p>上排 C，下排 M。来自实际录像第 0/45/90/135/180 帧，同一裁剪范围，未修改几何或光照。</p>'
for action in ['flexion','knee-flexion','seated-stop']:
 sheet=Image.new('RGB',(1250,520),'#eef2f6');draw=ImageDraw.Draw(sheet)
 for row,g in enumerate(['C','M']):
  for col,frame in enumerate([0,45,90,135,180]):
   src=a.run/g/(g+'-'+action)/('video/frame-%04d.png'%frame);im=Image.open(src).crop((160,180,670,700)).resize((250,255));sheet.paste(im,(col*250,row*260+5));draw.text((col*250+5,row*260+5),f'{g} / {frame}',fill='white',stroke_width=1,stroke_fill='black')
 sheet.save(a.out/(action+'-filmstrip.png'),optimize=True);sections+=f'<h3>{action}</h3><img loading="lazy" src="{action}-filmstrip.png">'
s=s.replace('<h2>验证范围与两轮身份</h2>',sections+'<h2>Editor 实跑</h2><div class="grid">'+''.join(editor_cards)+'</div><p><a href="compile-3.log">最终编译</a> · <a href="unit-2.log">4 项自动测试</a> · <a href="editor-identity.json">Editor 身份</a> · <a href="editor-runtime.log.gz">Editor 日志</a></p><h2>验证范围与两轮身份</h2>')
page.write_text(s,encoding='utf-8')
from html.parser import HTMLParser
class Links(HTMLParser):
 def handle_starttag(self,tag,attrs):
  for key,value in attrs:
   if key in ('src','href') and not value.startswith(('http:','https:','#','data:')):assert (a.out/value.split('#')[0]).exists(),value
Links().feed(s)
print(json.dumps(summary['M_sampled_geometry'],indent=2));print(page)
