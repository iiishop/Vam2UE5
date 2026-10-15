---
title: 发型还原与独立资源
subtitle: 原版 VaM 实测 → UE 原生 Groom · 先静态还原，再考虑物理
lang: zh
---
已修正 scalp frame、发束展开和颜色过渡。四个独立头发资产组成一个发型，再由人物引用。物理默认关闭。静态形状已恢复至可对照状态；跨引擎高光与透明覆盖仍有差异，不能标记为像素一致。

## A · 发型资源关系
```tree
BP_VamCharacter
  RuntimeConfiguration
    Hairstyle：独立发型组合
      ponybase1_2：独立头发资源
      lo_bangs11：独立头发资源
      lo_bangs12：独立头发资源
      pony_tail3：独立头发资源
    HairProfile：该体型的派生缓存
      Fine / Coarse Groom
      材质、参考身体、组合签名
```
源头发资产保留原始 guides、scalp、拓扑和材料参数，不持有角色 Body Mesh。一个发型可以引用一个或多个资源；同一资源可复用。角色适配后的 Groom 是缓存，不冒充可复用源头发。编辑组合后必须重新生成缓存，运行时明确拒绝旧缓存。

## B · 为什么原来不还原
- 原来只平移发根，没有传递头皮局部朝向。现按源拓扑的法线/切线和实际 scalp wrap 语义传递整条发束。
- 原来把 guide 当最终发丝，遗漏源渲染的卷曲、长度、三角形插值与 spread。这一版使用真实 VaM 静态导出重建发丝展开，共 38,923 条渲染曲线。
- 原来颜色过渡使用 u^rolloff；已按源公式改成 1 − (1−u)^(2^rolloff−1)。

## C · 实测导向线误差，单位 cm
| 资源 | RMS | P95 | Max |
|---|---:|---:|---:|
| ponybase1_2 | 0.0001055 | 0.0001778 | 0.0007070 |
| lo_bangs11 | 0.0000884 | 0.0001372 | 0.0003059 |
| lo_bangs12 | 0.0000912 | 0.0001417 | 0.0003904 |
| pony_tail3 | 0.0000938 | 0.0001491 | 0.0003675 |

与真实 VaM 关闭物理后的 particles 对比，使用实际 head 矩阵、源 curve ID 和 scalp 映射，不使用最近邻拟合。此表验证导向线坐标，不代表最终发丝材质/覆盖率误差。

## D · 构建和资源验证
- UE C++ 构建通过；新身份资产保存后在独立 Editor 进程重载通过。
- 13 项 Python 测试通过。真实 UAsset 的四资源引用、复用、组合删减/重排时旧缓存拒绝、恢复后绑定通过。
- 真实普通 BP 的 Game 五视角截图完成，4 个 Groom group 均关闭物理，活动 Niagara 为 0。
- 真正 Editor PIE 世界已加载同一普通 BP，五视角完成；[PIE 状态](pie-capture.json)、[PIE 正面截图](pie/front.png)。PIE 使用编辑器视口尺寸，不能作为下面同尺寸 Game 对比的替代。
- 生成身份：a44c67c5522c8b7fccb286589cde9ebb2f185902abf88573f7971854a6ac31fe。
- [资源与配置清单](bundle.json) · [导向线数据](guide-audit.json) · [真实 UAsset 测试](asset-audit.json) · [Game 状态](game-capture.json)。

## E · 对照条件与尚存差异
UE 修复前后使用相同角色、相机和光源。VaM 与 UE 使用相同头部相对相机及灯光方向，但不同引擎的光强单位、发丝 shader、抗锯齿和透明覆盖不等同，因此不声称光照像素一致。VaM 的花簪、夹子、口罩和衣服是另外的资源，本轮四个曲线资源不包含这些配饰。

当前可靠的卷曲/展开路径依赖真实 VaM 静态导出；仅凭文件解析复现所有源渲染步骤尚未完成。精细/粗略物理仍为后续工作，本轮不作为已验收功能。确认静态外观后再继续物理，不能用物理掩盖发型导入误差。

## F · 在当前工程查看
已备份旧 DLL 并安装新模块，实际 SmartNPC 工程的 [资产校验](main-project-audit.json) 通过。

内容浏览器打开 `/Game/VamHairRuntime/H_a44c67c5522c8b7fccb28658`，将其中正常的 `BP_VamCharacter` 放入关卡。它引用已验证身体与新发型，默认关闭头发物理。项目原来的默认角色选择未替换。

独立头发在 `/Game/VamHairLibrary/Hair`，发型组合在 `/Game/VamHairLibrary/Styles`。组合成员可查看或编辑；修改后需重新生成相应的身体适配缓存。已有保存的 Groom 可直接在 UE 使用，不需要同时运行 VaM；重新还原其他发型的源渲染曲线仍需要相应导出。
