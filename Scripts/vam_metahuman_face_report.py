"""Report diagnostic correspondence errors; these are not likeness scores."""
import json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'Saved/Python'))
import numpy as np
p=root/'Saved/MetaHuman/FitRepair'
anchors=json.loads((p/'face-anchors.json').read_text())
report={'units':'mm','scope':'saved source-surface face anchor residuals, not whole-face error or visual acceptance',
        'visual_acceptance_passed':False,'variants':{}}
for name in ['calibrated','refined','face_detail','face_solve','face_anchors','face_regularized','face_autocalibrated']:
    path=p/(name+'_posed.json')
    if not path.exists():continue
    v=np.asarray(json.loads(path.read_text())['vertices'])[:,[0,2,1]]
    errors={k:float(np.linalg.norm(v[int(k)]-target)*10) for k,target in anchors['keypoints'].items()}
    report['variants'][name]={'anchor_errors':errors,'mean':float(np.mean(list(errors.values()))),
                              'max':max(errors.values()),'nose_tip':errors['2925']}
(p/'face-comparison-report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
for name,item in report['variants'].items():print(name,'mean',round(item['mean'],3),'nose',round(item['nose_tip'],3))
