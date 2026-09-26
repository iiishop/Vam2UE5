"""Local, explicitly granted consent; character identity may vary, data scope may not."""
import json
from pathlib import Path

SCOPE_KEYS = ('schema', 'recipient', 'operation', 'fields_from_local_service_implementation',
              'excluded', 'texture_download')


def scope(disclosure):
    return {key: disclosure[key] for key in SCOPE_KEYS}


def standing_consent(saved, disclosure):
    path = Path(saved)/'MetaHuman/cloud-consent.json'
    try:
        consent = json.loads(path.read_text(encoding='utf8'))
        return (consent.get('granted') is True and consent.get('scope') == scope(disclosure)
                and bool(consent.get('user_instruction')))
    except (OSError, ValueError, KeyError):
        return False


if __name__ == '__main__':
    import sys
    value = json.loads(Path(sys.argv[2]).read_text(encoding='utf8-sig'))
    sys.exit(0 if standing_consent(sys.argv[1], value) else 1)
