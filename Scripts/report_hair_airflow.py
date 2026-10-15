"""Package matched UE hair airflow recordings and actual GPU audits."""
from pathlib import Path
import sys,json,shutil,subprocess,argparse
sys.path.insert(0,str(Path(__file__).resolve().parent))
from audit_hair_motion import summarize
parser=argparse.ArgumentParser()
parser.add_argument('run',type=Path);parser.add_argument('output',type=Path)
a=parser.parse_args();run=a.run;out=a.output;out.mkdir(parents=True,exist_ok=True)
rows=[]
for name in ['before','fine','coarse','pie']:
 take=run/name
 assert (take/'capture.json').exists(),take
 assert json.loads((take/'capture.json').read_text())['frames']==720
 audit=summarize(take/'capture.json');(out/(name+'-audit.json')).write_text(json.dumps(audit,indent=2))
 for g in range(4):
  r=[x for x in audit['measurements'] if x['group']==g]
  rows.append(f'| {name} | {g} | {max(x["motion_rms_cm"] for x in r):.3f} | {max(x["rest_max_cm"] for x in r):.3f} |')
 if name=='pie':
  assert audit['world']=='PIE'
  continue
 with (out/'encoding.log').open('a') as log:
  subprocess.run(['ffmpeg','-y','-framerate','30','-c:v','mjpeg','-i',str(take/'video/frame-%04d.png'),'-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out/(name+'.mp4'))],stdout=log,stderr=subprocess.STDOUT,check=True)
 for f in [0,180,375,540]:shutil.copy2(take/f'video/frame-{f:04d}.png',out/f'{name}-{f}.jpg')
if (out/'fine.mp4').exists():
 with (out/'encoding.log').open('a') as log:
  subprocess.run(['ffmpeg','-y','-i',str(out/'before.mp4'),'-i',str(out/'fine.mp4'),'-filter_complex','hstack=inputs=2','-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out/'comparison.mp4')],stdout=log,stderr=subprocess.STDOUT,check=True)
with (out/'encoding.log').open('a') as log:
 subprocess.run(['ffmpeg','-y','-ss','11.5','-i',str(out/'comparison.mp4'),'-t','7','-vf','setpts=2*PTS','-an','-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out/'jump-slow.mp4')],stdout=log,stderr=subprocess.STDOUT,check=True)
bundle=json.loads((run/'bundle-01.json').read_text());shutil.copy2(run/'bundle-01.json',out/'bundle.json')
doc='''---
title: 头发相对气流与散发响应
subtitle: 同动作、同灯光、360° 环绕 · UE 实际输出
lang: zh
---
## A · 本轮实现
改用 UE Groom 原生世界空间求解，节点速度直接包含头部平移与转动。环境空气速度仍为零；没有额外添加持续吹风，也没有叠加第二套头部惯性。

原局部空间路径的阻力比较局部节点速度与旋转后的空气速度，未减去参考骨的平移/转动速度。原生局部惯性项处理加速度，不能替代匀速行进时的迎风阻力。

散发形状约束从根部 3 Hz 平滑降至末端 1.2 Hz；根部仍由原生绑定固定。发髻 35 Hz、贴头组 45 Hz 及其位移限幅保留。此频率为工程控制参数，不是实测人体材料常数。

## B · 看视频
对比视频左侧为旧版局部空间、旧约束，右侧为新版世界空间、渐变约束。两项同时变化，因此该对比不能独立量化空气阻力和约束各自的贡献。

镜头随动作绕一周：背面 → 侧面 → 正面 → 另一侧 → 背面。0–1 秒静止，1–5 秒起步/走路，5–7 秒转身，7–11 秒加速跑步，11 秒急停，12–14 秒跳跃落地，15–19 秒蹲起，19 秒后走路再急停。跳跃时镜头接近正面。

录像是已记录的真实骨骼矩阵和角色位移回放。30 fps 编码不代表实时性能达到 30 fps。

## C · 实际 GPU 导向线
每秒采样一次，单位 cm。运动 RMS 是相对首帧的最大采样 RMS；偏移 Max 是相对静止导向线的最远采样点。采样不保证捕获所有瞬时峰值或排除穿透。

| 版本 | 组 | 运动 RMS 最大 | 静止形状偏移 Max |
|---|---:|---:|---:|
'''+ '\n'.join(rows)+'''

## D · 资产与边界
角色：`'''+bundle['blueprint']+'''`。

静态导入的发丝、宽度和资源组合沿用已还原版本，重新生成求解资产身份。精细 128 条/组、32 节点；粗略 32 条/组、16 节点；两档均有分组物理。

依然是保守球/胶囊碰撞，不是精确头皮或完整发丝自碰撞。世界空间求解需要另行验证远离世界原点和原点重置；当前录像不能证明任意发型都通过。

[新资产清单](bundle.json) · [旧版读回](before-audit.json) · [新版精细读回](fine-audit.json) · [新版粗略读回](coarse-audit.json) · [PIE 读回](pie-audit.json)
'''
(out/'report.md').write_text(doc,encoding='utf8')
renderer='C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py'
subprocess.run([sys.executable,renderer,'render',str(out/'report.md'),'--output',str(out/'report.html'),'--no-open'],check=True)
p=out/'report.html';s=p.read_text(encoding='utf8');media='<section style="max-width:1400px;margin:24px auto"><h2>左：旧版　右：新版精细</h2><video controls style="width:100%" src="comparison.mp4"></video><h2>新版精细 · 环绕完整动作</h2><video controls style="width:100%" src="fine.mp4"></video><h2>正面跳跃 · 半速对比（左旧右新）</h2><video controls style="width:100%" src="jump-slow.mp4"></video><h2>新版粗略 · 环绕完整动作</h2><video controls style="width:100%" src="coarse.mp4"></video></section>';p.write_text(s.replace('</body>',media+'</body>'),encoding='utf8')

