from pathlib import Path
import csv,json,statistics
from PIL import Image
root=Path(__file__).resolve().parents[1]/'Saved/ContactTwoCharacters'
results={}
for run in ['run3','run4']:
 path=root/run/'frames.csv'
 if not path.exists():continue
 assert Image.open(root/run/'phase-2.png').size==(1920,1080), 'Actual viewport must be 1080p'
 rows=list(csv.DictReader(path.open()))
 results[run]={}
 for label in dict.fromkeys(x['phase'] for x in rows):
  phase=[x for x in rows if x['phase']==label];v=[float(x['frame_ms']) for x in phase];v.sort()
  results[run][label]={'frames':len(v),'mean_ms':statistics.mean(v),'mean_fps':1000/statistics.mean(v),'p95_ms':v[int(.95*len(v))],'p99_ms':v[int(.99*len(v))],'solver_counts':sorted(set(int(x['active_solvers']) for x in phase)),'max_contact_cm':max(float(x['max_contact_cm']) for x in phase)}
(root/'results.json').write_text(json.dumps(results,indent=2))
labels={'off_before':'Chaos 关闭，Jiggle 保留','on_idle':'Chaos 开启，无接触 / 自动休眠','on_pressed':'两人物各一个球同时按压','off_after':'受压后关闭 Chaos'}
rows=''
for phase in labels:
 rows+='<tr><td>'+labels[phase]+'</td>'
 for run,data in results.items():
  a=data[phase];rows+=f'<td>{a["mean_fps"]:.1f}</td><td>{a["mean_ms"]:.2f}</td><td>{a["p95_ms"]:.2f}</td>'
 rows+='</tr>'
header='<tr><th>状态</th>'+''.join(f'<th>{run} FPS</th><th>平均 ms</th><th>P95 ms</th>' for run in results)+'</tr>'
html='''<!doctype html><meta charset="utf-8"><title>双人物实际帧率</title><style>body{font:17px/1.7 system-ui;color:#e7edf6;background:#151c26;max-width:1200px;margin:40px auto;padding:0 24px}h2,a{color:#8fc7ff}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}td,th{padding:9px;border-bottom:1px solid #405064;text-align:left}img{max-width:100%}</style><h1>两个 BP 人物 · 实际游戏帧率</h1><p>2026-10-09 · i5-12600KF / RTX 4070 Ti · UE 5.8.3 Development Editor -game，DX12，1920×1080，High（sg=2），屏幕比例 100%，关闭垂直同步与帧率上限。</p><p>在独立 Entry 简单场景中生成两个同配置的 BP_VamCharacter，保持 Jiggle 默认开启；两盏灯、固定镜头。每阶段预热 120 帧（首阶段 180），随后采样 300 帧。使用正常引擎游戏 Tick / 视口渲染，没有手动推进世界或同步 SceneCapture。截图在正式计时开始前 40 帧请求，像素尺寸及引擎日志均核验为 1920×1080。run1/run2 为窗口自动缩放到 888×500 的预试验，保留原始记录但不计入下表。</p><table>'''+header+rows+'''</table><h2>如何解读</h2><ul><li>FPS = 1000 / 平均实际帧间隔，来自正常引擎循环时间戳，不是组件 ms 的倒数。</li><li>两个人物同时受压时记录到两个活动求解器。开关开启但无接触时为零：当前系统自动回到原生蒙皮。</li><li>受压为每个人物一个胸部受到独立运动学刚性球的静态按压，并非两个角色互相软体碰撞，也不是两个人四侧同时被按压。</li><li>这是简单场景的双人物基准，不含完整游戏场景、服装/头发模拟及游戏逻辑。不能将空场景无接触 FPS 当作完整游戏承诺。</li><li>不是打包 Shipping 或显示器实际呈现测量：采用离屏游戏视口，GPU 仍渲染；没有开启 Insights 采样来影响本轮帧率。</li></ul><h2>实际视口验证</h2><img src="run4/phase-2.png"><p><a href="results.json">结果 JSON</a> · <a href="run3/frames.csv">首轮全部帧</a> · <a href="run4/frames.csv">第二轮全部帧</a> · <a href="run4/command.json">完整启动参数</a></p>'''
(root/'compare.html').write_text(html,encoding='utf-8')
print(json.dumps(results,indent=2))
