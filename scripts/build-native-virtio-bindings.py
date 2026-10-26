#!/usr/bin/env python3
"""Compatibility entry for preserved fuxi candidate build scripts."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('build-virtio-mmio-bindings.py')), run_name='__main__')
