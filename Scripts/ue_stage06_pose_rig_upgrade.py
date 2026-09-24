"""Migrate rig limits through an independently reloaded runtime bundle.
An explicit recipe revision produces a new asset path, so a locked legacy rig
cannot silently fall back to its unsaved limits or overwrite a user's derivative.
"""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from ue_runtime_build import main
if __name__=='__main__':main()
