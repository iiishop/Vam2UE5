# Stage06 — 运行时人物、原生外观、IK 与运动/惯性底座

## 当前状态

Stage06 的运行时纵向链已建立。Stage06 人物现引用补齐眼睑 Morph 的 Stage05 新配方 `/Game/VamCharacters/C_1facd7a930a8e1eca11a18a6`；旧六参数资产未被覆盖。扩展资产写入 `/Game/VamStage06`。新配方的 Editor Game 已验证左右眨眼；Cooked 复验见下方证据。尚未通过或尚未验证的能力不得算作完成。

## 可放置资产与职责

| 资产/组件 | 职责 |
|---|---|
| `/Game/VamStage06/BP_VamCharacter` | 可放置 Actor；指定 Stage05 CharacterDefinition、Rig、AnimBP、Physics Asset、材质配置。 |
| `/Game/VamStage06/DA_Rig_Eyelids_a11a18a6` | 58 个语义骨骼映射，绑定新 Stage05 Skeleton；IK 骨名由此资产确定。 |
| `/Game/VamStage06/ABP_Eyelids_a11a18a6` | 绑定新 Skeleton 的动画蓝图子类；使用本插件的 PBIK 姿态代理。 |
| `/Game/VamStage06/PA_Eyelids_a11a18a6` | 新网格的 28 个刚体、27 条连通关节；线位移锁定，肘膝等角度限制独立设置。 |
| `/Game/VamStage06/DA_NativeMaterials` | 身体 30 个材质槽、13 个部件的 Stage06 自有材质覆盖；源贴图仍按颜色语义复用。 |
| `/Game/VamStage06/L_Stage06Preview` | 两个人物、固定主补光、地面、PIE 鼠标工具。 |
| `/Game/VamStage06/L_Stage06Automated` | Standalone/Cooked 运行时验证关卡；仅带 `-VamStage06Acceptance` 参数才执行并退出。 |

核心逻辑放在 Runtime 模块：`UVamCharacterComponent` 负责资产/形状/外观所有权，`UVamMotionComponent` 负责运动历史与固定时钟，`UVamInteractionComponent` 负责 Physics Handle 与模式切换，`UVamActivePoseComponent` 负责呼吸/注视/下颌/Idle。`AVamCharacterActor` 是不依赖 CharacterMovement 的展示容器。`AVamPreviewTool` 只提供 PIE 输入与绘制，不拥有角色状态。两个角色各自持有动态材质实例、运动历史、IK 目标和物理组件。

## 每帧数据顺序

```mermaid
flowchart LR
    I[时间戳轨迹/拖动] --> M[Motion: 速度与加速度/瞬移分类]
    M --> W[固定 120 Hz 惯性见证]
    S[ShapeState commit] --> B[实例参考绑定与 Morph]
    A[主动呼吸/注视/下颌] --> P[AnimBP: 参考姿态与形状骨骼]
    B --> P
    P --> K[PBIK 可达目标与关节限位]
    K --> R[Physics Asset / Physical Animation / Handle]
    R --> D[最终骨骼蒙皮与部件跟随]
    W --> F[Stage07/08/09 运动输入接口]
    M --> F
```

`Body` 的动画 Tick 明确依赖 Motion 与 ActivePose 组件。PBIK 禁止骨骼伸长，使用 Rig 映射内的肘膝 preferred bend 和限位；世界空间目标包含旋转，`SetFootLocked` 保存脚的世界目标。IK 自身不做墙面、地面或身体避障接触查询。

`FVamMotionSample` 输出线/角速度、线/角加速度和显式瞬移标志。瞬移清除测试区位移/速度，只广播一次瞬移样本。`FVamSolverClock` 记录固定子步、插值、丢弃步数、暂停、shape revision、瞬移 revision 与 warm-up。超过每帧 8 步的时间不会一次注入大 delta。`FVamCharacterState` 区分动画姿态、刚体姿态、表面和碰撞代理的时间；未接入的表面输出时间为 `-1`，不会热路径读取 GPU 顶点。

黄色两个区域是低成本惯性**见证**：对实际组件平移和旋转加速度做弹簧阻尼响应，用于检验 Stage07/08/09 的输入时序；不是肌肉、脂肪或软体表面。其输入有上限，原始运动样本仍保留真实速度/加速度。

## 交互

在 `L_Stage06Preview` 进入 PIE：左键拖动人物上的青色骨点。若骨点有模拟刚体，使用有限刚度的 Physics Handle 抓取；否则调整临时局部姿态。右键拖动人物控制点，走连续 `MoveContinuously` 路径。松开鼠标释放抓取。`G` 切布娃娃，`C` 回受控，`P` 暂停惯性见证时钟，`O` 单步，`R` 重置。编辑器插件的“人物调试”面板也显示运动/物理状态，并提供相应按钮；编辑器 Gizmo 移动不等同于模拟运动。

`SetIKGoal` 可用于头、胸、骨盆和双手足；`SetFootLocked` 保存脚部世界目标。`SetPhysicalMode` 支持受控、局部响应、布娃娃；局部响应使用 Physical Animation 的强度/阻尼与物理混合。抓取使用 Physics Handle 的目标约束，并从目标骨骼向上启用短祖先链的模拟，使肘肩或膝髋可响应，避免只模拟手掌/脚掌时被锁定父关节限制。`ActivePose` 将同一 `BlinkWeight` 写入新 Stage05 的左右闭眼 Morph。`SetAppearanceScalar/Color` 修改角色私有 MID，不修改共享材质。

## 原生外观

Stage06 材质从来源参考图复制并在新包内独立维护，保留原 BaseColor、法线、UV 与透明连接。按有证据的来源区域分为皮肤、清澈眼部层、眼球、口腔、指甲和衣料；为粗糙度、高光及连接至 BaseColor 的 `Tint` 建立参数，皮肤使用 UE Subsurface 着色；每个角色创建私有 MID。未映射来源参数没有伪称为等价 UE 参数。固定主补光在 `L_Stage06Preview`，需要用真实图形编辑器检查接缝、眼部叠层、法线和肤色。

## 自动证据

| 验证 | 结果 |
|---|---|
| `Saved/NativeBuild/stage06-runtime-check.json` | 通过：手部 IK 约 15.43 cm，第二人 0 cm；27 条物理约束；两人 MID 和 `Tint` 参数隔离；连续运动与瞬移状态。 |
| `Saved/NativeBuild/stage06-motion-check.json` | 通过：加速/匀速/减速/停止、左右急转、抬放、纯旋转在 30/60/120 FPS 的峰值差异小于预设 `max(2 cm, 20%)`，停稳后位移 < 0.5 cm。0.25 s 卡顿只执行 8 步，丢弃 21 步。 |
| Standalone `Saved/Stage06Acceptance.json` | 通过：实际 Play 世界中的 IK、手部刚体随抓取目标位移、释放、布娃娃、受控恢复和瞬移。 |
| Windows Cooked `Saved/stage05-blink-stage06-packaged-runtime.log` | 新 Stage05 人物重验通过：`runtime_chain_verified`，左右闭眼权重峰值均超过 0.99，28 刚体、27 约束；打包日志 `Saved/stage05-blink-stage06-package.log` 为 `BUILD SUCCESSFUL`。 |

## 尚未验收的范围

- Stage05 新配方已有来源左右闭眼 Morph；运行时两侧权重均达到约 0.999。真实 GPU 画面中的眼睑闭合、睫毛与角膜叠层仍需目视验收。
- 足底锁定是世界目标保持；墙面、地面、身体避障需要额外接触查询和约束。不能把 PBIK 视为完整接触物理。
- `P/O/R` 与面板的暂停/单步/重置作用于 Stage06 固定惯性见证时钟；Chaos 刚体的整世界暂停和单步尚未接入。
- 已验证抓取刚体的可测位移与受控恢复；关闭主动动作后的 Chaos 长时间接触静态平衡仍缺少独立数值验收。
- 尚未建立 Stage07 全身肌肉/软体、Stage08 衣服、Stage09 头发求解器；黄色见证点只验证输入与时序。
- 当前 AnimBP 的基础输入是形状参考姿态和主动微动作；尚未接入通用行走动画状态机或运动角色 Pawn/Character 薄包装。现有展示 Actor 不依赖 CharacterMovement。
- 真实图形环境下的材质接缝/眼部视觉比对尚未完成；命令行 `-nullrhi` 验证了 Cooked 运行链，但无法替代目视检查。
