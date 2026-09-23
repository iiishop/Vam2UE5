"""Narrow aliases for ZIP members stored with unflagged GB18030 names.

Python correctly follows ZIP's CP437 default. Some real VARs instead wrote
Chinese filename bytes without the UTF-8 flag. Keep the original name and add
an alias only when a strict GB18030 decode yields CJK text; never guess a file
by basename or silently choose between two distinct archive members.
"""


def member_aliases(info):
    yield info.filename
    if info.flag_bits & 0x800:
        return
    try:
        decoded = info.filename.encode('cp437').decode('gb18030')
    except UnicodeError:
        return
    if decoded != info.filename and any('\u3400' <= char <= '\u9fff' for char in decoded):
        yield decoded


def find_member(archive, path):
    matches = []
    key = path.casefold()
    for info in archive.infolist():
        if not info.is_dir() and any(alias.casefold() == key for alias in member_aliases(info)):
            matches.append(info)
    if len(matches) != 1:
        raise ValueError(('Ambiguous' if matches else 'Missing') + ' ZIP member: ' + path)
    return matches[0]
