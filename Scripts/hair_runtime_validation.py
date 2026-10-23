"""Reject shader failures even if Unreal logs them as warnings and exits zero."""
import re

FAILURE = re.compile(r"(?:error [Xx]\d{4}:|Log(?:Niagara|ShaderCompilers): Error:|Failed to compile.*(?:Niagara|shader)|Niagara.*failed.*compil)", re.I)

def shader_failures(text):
    return [line for line in text.splitlines() if FAILURE.search(line)]
