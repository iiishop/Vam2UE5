"""Reload a pinned build; override mutation is performed outside UE's locked file handles."""
import json,os,sys
from pathlib import Path
import unreal as u
scripts=Path(__file__).resolve().parent;sys.path.insert(0,str(scripts))
from ue_native_character import build
data=scripts.parent/'Saved'
report=json.loads(Path(os.environ['VAM_NATIVE_REPORT_FILE']).read_text(encoding='utf8'))
assert report['independent_reload_verified']
os.environ['VAM_NATIVE_PREVIEW_FILE']=str(data/'Decoded'/(report['source_digest']+'.appearance.preview.json'))
if os.environ.get('VAM_EXPECT_OVERRIDE'):
    try:build(data)
    except AssertionError as exc:
        assert 'User-modified native asset protected' in str(exc),str(exc)
        u.log('VAM_SHAPE_OVERRIDE_PROTECTED')
    else:raise AssertionError('Modified asset was not protected')
else:
    first=build(data);again=build(data)
    assert first['folder']==report['folder']==again['folder'] and again['assets']==first['assets']
    u.log('VAM_SHAPE_REIMPORT_OK')
