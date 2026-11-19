"""Generates the tray icons (knob seen from above, blue ring when active)."""
from PIL import Image, ImageDraw, ImageFilter
import os

def knob(size, active):
    s = size * 4
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if active:
        glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse((0, 0, s - 1, s - 1), fill=(40, 140, 255, 255))
        im = Image.alpha_composite(im, glow.filter(ImageFilter.GaussianBlur(s * 0.04)))
        d = ImageDraw.Draw(im)
    m = s * 0.08
    d.ellipse((m, m, s - m, s - m), fill=(60, 140, 255) if active else (130, 130, 130))
    m2 = s * 0.18
    d.ellipse((m2, m2, s - m2, s - m2), fill=(185, 190, 198) if active else (160, 160, 160))
    m3 = s * 0.26
    d.ellipse((m3, m3, s - m3, s - m3), fill=(215, 218, 224) if active else (185, 185, 185))
    return im.resize((size, size), Image.LANCZOS)

here = os.path.dirname(os.path.abspath(__file__))
sizes = [16, 20, 24, 32, 40, 48, 64, 256]
for name, active in (("powermate.ico", True), ("powermate-off.ico", False)):
    big = knob(256, active)
    big.save(os.path.join(here, name), sizes=[(n, n) for n in sizes],
             append_images=[knob(n, active) for n in sizes])
