"""Compatibility entry point: runtime upgrades now use explicit native recipes.
See Config/Examples. The previous sample output is preserved; no implicit overwrite.
"""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from ue_runtime_build import main
if __name__=='__main__':main()
