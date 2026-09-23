"""Pin a native build to one immutable preview despite concurrent browsing."""
import json
import os
from pathlib import Path


def load_preview(data):
    selected = os.environ.get('VAM_NATIVE_PREVIEW_FILE')
    path = Path(selected) if selected else Path(data) / 'Decoded/latest.json'
    preview = json.loads(path.read_text(encoding='utf8'))
    decode_id = preview['decode_id']
    if len(decode_id) != 64 or any(c not in '0123456789abcdef' for c in decode_id):
        raise ValueError('Invalid immutable decode identifier')
    if not selected:
        immutable = Path(data) / 'Decoded' / (decode_id + '.appearance.preview.json')
        if not immutable.exists():
            raise ValueError('Parse source materials before building native assets')
        preview = json.loads(immutable.read_text(encoding='utf8'))
        if preview['decode_id'] != decode_id:
            raise ValueError('Immutable preview identity mismatch')
        os.environ['VAM_NATIVE_PREVIEW_FILE'] = str(immutable.resolve())
    return preview
