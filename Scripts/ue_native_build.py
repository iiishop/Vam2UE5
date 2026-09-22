"""Separate editor operation. Source checks cannot be bypassed by a preview Actor.

Current actual VaM TriAx characters stop at a diagnostic report. Native builder
and runtime are independently exercised with legal synthetic geometry.
"""
import json
import os
from pathlib import Path
import sys
import traceback
import unreal

SCRIPTS=Path(__file__).resolve().parent
if str(SCRIPTS) not in sys.path:sys.path.insert(0,str(SCRIPTS))
from vam_native_source import prepare_file


def run():
    data=SCRIPTS.parent/'Saved'
    preview=json.loads((data/'Decoded/latest.json').read_text(encoding='utf8'))
    decode_id=preview['decode_id']
    if len(decode_id)!=64 or any(c not in '0123456789abcdef' for c in decode_id):
        raise ValueError('Invalid immutable decode identifier')
    result,output=prepare_file(data/'Decoded'/(decode_id+'.ir.json'),data/'NativeBuild')
    blockers=result['blockers']
    if not blockers:
        # Do not equate passing source analysis with transactional native import.
        blockers=[{'code':'native_commit_pending','source':decode_id,
                   'detail':'正式来源资产提交、独立重载验证及 ImportState 更新尚未接通。'}]
    lines=['当前人物尚不能提交为正式骨骼资产：']
    labels={'triax_pending_calibration':'TriAx 蒙皮缺少已验证的 UE 权重适配',
            'formula_adapter_pending':'骨骼/比例公式尚未实现，不能全部当顶点 Morph',
            'source_hierarchy_pending':'来源 Transform 父链尚未完整保留，需要补齐绑定依据',
            'nonlinear_graft_pending':'graft 非线性参数尚未实现',
            'native_commit_pending':'资产事务和重载验证尚未接通'}
    lines.extend('• '+labels.get(b['code'],b['code']) for b in blockers)
    lines.extend(['','没有创建正式人物，没有更新 ImportState。',
                  '原有临时预览仍可使用。', '', '详细报告：'+str(data/'NativeBuild/latest-report.json')])
    unreal.log_warning('Stage05: '+json.dumps(blockers,ensure_ascii=False))
    if not os.environ.get('VAM_NATIVE_UNATTENDED'):
        unreal.EditorDialog.show_message('生成 UE 人物资产：待校准','\n'.join(lines),unreal.AppMsgType.OK)
    return result


try:
    run()
except Exception:
    message=traceback.format_exc()
    unreal.log_error(message)
    if not os.environ.get('VAM_NATIVE_UNATTENDED'):
        unreal.EditorDialog.show_message('生成 UE 人物资产失败',message,unreal.AppMsgType.OK)
