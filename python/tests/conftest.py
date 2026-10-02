"""
Shared pytest configuration for the Stage 1 Python test suite.

Adds src/ to sys.path so tests can do `import datalake`, `import
metadata`, etc. exactly like control.py and the other modules already
do amongst themselves - no package installation needed.
"""

import sys
from pathlib import Path

SRC_DIR = Path(__file__).resolve().parent.parent / "src"
sys.path.insert(0, str(SRC_DIR))
