#!/usr/bin/env python3
"""Makes the looping test video tracking_ab.py feeds a detector: tests/data/bus.jpg panned back and forth two pixels a frame, 30 fps, MJPG.
Usage: make_bus_pan.py bus.jpg bus-pan.avi   (needs opencv-python)"""
import sys, cv2
photo = cv2.imread(sys.argv[1])
height, width = photo.shape[:2]
crop = width - 100
out = cv2.VideoWriter(sys.argv[2], cv2.VideoWriter_fourcc(*'MJPG'), 30, (crop, height))
xs = list(range(0, 100, 2)) + list(range(98, 0, -2))
for _ in range(6):
    for x in xs:
        out.write(photo[:, x:x + crop])
out.release()
