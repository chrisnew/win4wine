# XP-Style Icon Guide

Nov 27, 2026 · @Chris

## The look

Our XP-style icons are soft, glossy, brightly lit objects with a crisp blue-gray outline. These rules come from the .iso document-and-CD icon.

&#91;image: Reference icon for .iso files: a document with a CD\]

- **One light source:** upper left. Highlights go top-left, shadows fall down and to the right.
- **Gradients everywhere:** every surface shades from light to darker. Avoid flat fills.
- **Colored outlines:** use darker blue-gray, never pure black.
- **Soft drop shadows:** navy-tinted, offset a few pixels, blurred at large sizes.
- **Gloss:** a translucent white crescent or inner highlight on shiny parts.
- **Simple composition:** a base object plus one front object at lower right.
- **Original art:** draw your own shapes. Leave out Microsoft logos and don't trace Windows icons.

## Canvas and layout

Draw large icons on a 256 × 256 grid, with the origin at top left.

| Element | Position and size (units) |
| --- | --- |
| Page | x 34 to 190, y 8 to 232 |
| Folded corner | 50 × 50, top right of the page |
| Front object (the CD) | center 162, 163; radius 80 |
| Text lines | x 56; y 40 + 19 per line; height 6; widths 40 to 112 |
| Outline | 2.4 wide, or 1 output pixel if that is larger |

Stack the layers in this order, bottom first:

1. Page shadow
2. Page with gradient and outline
3. Inner white highlight and text lines
4. Fold shadow, then the curled corner flap
5. Front object's shadow
6. Front object's body, shading and details
7. Gloss, then the front object's outline

## Colors and effects

Reuse these values so new icons match. Blur is the Gaussian standard deviation on the 256 grid. Gradients run top left to bottom right unless noted.

| Part | Value |
| --- | --- |
| Paper | #ffffff, #fbfdff at 50%, #c3d3ee |
| Page outline | #8298bd to #3f547d |
| Corner flap, bottom left to top right | #ffffff, #dfe9fa at 45%, #8fa8d2 |
| Inner highlight | white, 3 wide, inset 3.5 |
| Text lines | #7f9fd6 at 45% opacity |
| Page shadow | #10203c, 34%, offset 5, 6, blur 4.5 |
| Fold shadow | #10203c, 22%, blur 1.6 |
| Front object shadow | #0b1830, 42%, offset 4, 6, blur 4.5 |
| Front object outline | #8b9bb5 to #3d4d6d, 2.4 wide |
| Sheen, radial from 33%, 26%, radius 80% | white 65%, clear at 55%, #31486e 22% at edge |
| Gloss crescent | white, 45% opacity, upper left |

### CD-only details

| Part | Value (r = disc radius) |
| --- | --- |
| Silver base | blends between #a4b4ce and #e8f0fb around the disc, lightest at 45° and 225° |
| Rainbow fans | centers 225° and 45°; falloff widths 42° and 38°; strength 97% and 92% |
| Rainbow color | HSV saturation 78%, value 100%; hue shifts across each fan |
| Grooves | white, 1 wide, at 0.86r, 0.70r, 0.54r; opacity 30%, 20%, 28% |
| Rim highlight | white 75%, 2.4 wide, at r − 3 |
| Hub | radius 0.34r; #ffffff to #a9b8d0 at 88%; outline #7b8dab, 1.8 wide |
| Hub ring | white 90%, 3 wide, at 0.23r |
| Center hole | radius 0.14r, transparent; ring #586a8c, 2.4 wide |

Angles run clockwise from 3 o'clock.

## Making a new icon

Keep the page, light and shadows, and change only the front object. This keeps a set of file icons consistent.

1. Pick one recognizable object, such as a disc, gear, padlock or music note.
2. Fit it in the lower right, about 60% of the canvas wide.
3. Build it from simple shapes, each with a light-to-dark gradient.
4. Give it the outline, shadow and gloss values above. Choose one main color family.
5. Redraw the small sizes by hand instead of only scaling down.
6. Check every size at 100% on both white and mid-blue backgrounds.

### Small sizes

Coordinates below are in output pixels. Half-pixel edges keep 1 px outlines sharp.

| Size (px) | Page: left, top, right, bottom | Fold | Disc: center x, y; radius |
| --- | --- | --- | --- |
| 16 | 1.5, 0.5, 11.5, 14.5 | 3.5 | 10, 10; 5.5 |
| 20 | 2.5, 0.5, 14.5, 18.5 | 4 | 12.5, 12.5; 7 |
| 24 | 2.5, 0.5, 17.5, 22.5 | 5 | 15, 15; 8.5 |
| 32 | 4.5, 1.5, 23.5, 29.5 | 7 | 20, 20; 11 |

- **16 to 32 px:** use a straight triangular fold. Drop blur, gloss and grooves, and simplify the hub.
- **Shadows:** offset the front shadow 0.4, 0.6 at 25%. From 24 px, add a page shadow offset 1, 1 at 22%.
- **Text lines:** omit at 16 and 20 px. Use 1 px lines at 24 and 32 px.
- **40 px and up:** scale the large artwork.

## Exporting to .ico

Export one transparent PNG per size, then pack them with the script below.

1. Save PNGs named `16.png`, `20.png`, `24.png`, `32.png`, `40.png`, `48.png`, `64.png`, `96.png`, `128.png` and `256.png`.
2. For 40 to 128 px, thicken outlines to at least 1 output pixel (256 ÷ size, in grid units).
3. Install Pillow with `pip install pillow`.
4. Save the script as `make_ico.py` beside the PNGs and run `python make_ico.py`.

The script writes `icon.ico`. Sizes below 256 px are stored as 32-bit bitmaps, which Windows XP can read. The 256 px image is stored as PNG.

```python
import io, struct
from PIL import Image

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
blobs = []
for s in SIZES:
    im = Image.open(f"{s}.png").convert("RGBA")
    assert im.size == (s, s), f"{s}.png must be {s} x {s}"
    if s == 256:                                   # stored as PNG
        out = io.BytesIO()
        im.save(out, "PNG")
        blobs.append(out.getvalue())
        continue
    flip = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    pixels = flip.tobytes("raw", "BGRA")           # 32-bit, bottom row first
    row = (s + 31) // 32 * 4                       # mask rows align to 4 bytes
    mask = bytearray(row * s)
    for y in range(s):
        for x in range(s):
            if flip.getpixel((x, y))[3] == 0:
                mask[y * row + x // 8] |= 0x80 >> x % 8
    head = struct.pack("<IiiHHIIiiII", 40, s, s * 2, 1, 32, 0,
                       len(pixels) + len(mask), 0, 0, 0, 0)
    blobs.append(head + pixels + bytes(mask))

offset = 6 + 16 * len(SIZES)
index = struct.pack("<HHH", 0, 1, len(SIZES))
for s, blob in zip(SIZES, blobs):
    index += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32,
                         len(blob), offset)
    offset += len(blob)
with open("icon.ico", "wb") as f:
    f.write(index + b"".join(blobs))
```

## Reusable prompt

To have an AI assistant make the next one, attach this guide and `iso-disc.svg`. Then send this prompt with the brackets filled in.

```text
Make an original Windows XP-style icon for [file type] showing a document
with [object]. Follow the attached style guide and match iso-disc.svg:
same page, light direction, outlines and shadows. Replace only the CD,
and use [color] as the object's main color. Hand-fit the 16, 20, 24 and
32 px versions. Deliver an SVG, a PNG preview and a multi-size .ico.
Do not copy Microsoft icons or logos.
```

## Final checklist

- [ ] Light comes from the upper left on every part
- [ ] The object is recognizable at 16 px
- [ ] Outlines are crisp on white and mid-blue backgrounds
- [ ] The icon matches the others in the set
- [ ] The `.ico` holds all ten sizes with transparent backgrounds
