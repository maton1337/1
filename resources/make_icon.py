# Renders the BridgeRack app icon (resources/icon.png, 1024x1024, macOS rounded-square layout).
# Usage: python3 resources/make_icon.py resources/icon.png [path/to/Bold.otf]
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import sys

S = 1024
M = 100            # macOS icon grid margin
R = 185            # corner radius
out = sys.argv[1]
font_path = sys.argv[2] if len(sys.argv) > 2 else "/usr/share/fonts/opentype/inter/InterDisplay-Bold.otf"

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

# Drop shadow
shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(shadow).rounded_rectangle((M, M + 14, S - M, S - M + 14), R, fill=(0, 0, 0, 150))
img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18)))

# Body: charcoal console-metal gradient
body = Image.new("RGBA", (S, S))
bd = ImageDraw.Draw(body)
for y in range(S):
    t = y / S
    bd.line((0, y, S, y), fill=(int(58 - 36 * t), int(63 - 39 * t), int(72 - 44 * t), 255))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((M, M, S - M, S - M), R, fill=255)
img.paste(body, (0, 0), mask)

d = ImageDraw.Draw(img)
d.rounded_rectangle((M, M, S - M, S - M), R, outline=(255, 255, 255, 40), width=3)

# Letters
font = ImageFont.truetype(font_path, 400)
text = "BR"
box = d.textbbox((0, 0), text, font=font)
tx = (S - (box[2] - box[0])) / 2 - box[0]
ty = 250 - box[1]
d.text((tx + 4, ty + 8), text, font=font, fill=(0, 0, 0, 120))
d.text((tx, ty), text, font=font, fill=(236, 240, 245, 255))

# Horizontal LED level meter under the letters: green, then yellow, then red, the last segments unlit.
segments, lit = 12, 9
left, right, top, bottom = 210, 814, 742, 800
gap = 10
w = (right - left - gap * (segments - 1)) / segments
for i in range(segments):
    col = (61, 220, 132) if i < 7 else (240, 180, 41) if i < 10 else (229, 83, 75)
    if i >= lit:  # unlit LEDs: the colour, dimmed into the dark body (not transparent)
        col = tuple(int(c * 0.22 + 26 * 0.78) for c in col)
    x = left + i * (w + gap)
    d.rounded_rectangle((x, top, x + w, bottom), 6, fill=col + (255,))

# Accent rule in the console blue above the meter.
d.rounded_rectangle((210, 700, 814, 712), 6, fill=(63, 169, 245, 255))

img.save(out)
