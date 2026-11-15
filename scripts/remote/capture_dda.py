"""Saves a Desktop Duplication frame of the primary screen. Run by jobs/screenshot.ps1 with the venv that setup_capture.ps1 installs.
usage: capture_dda.py OUT.png   prints size= and brightness= (mean of a coarse grid, like the GDI fallback); exit 3 if no frame."""
import sys
import time

import dxcam
from PIL import Image

cam = dxcam.create(output_color="RGB")
frame = None
# grab() returns None until the screen changes; a fresh instance gives its first frame at once, so retry briefly.
for _ in range(60):
    frame = cam.grab()
    if frame is not None:
        break
    time.sleep(0.05)
if frame is None:
    sys.exit(3)
Image.fromarray(frame).save(sys.argv[1])
height, width = frame.shape[:2]
print("size=%dx%d" % (width, height))
print("brightness=%.1f" % float(frame[::40, ::40, :3].mean()))
