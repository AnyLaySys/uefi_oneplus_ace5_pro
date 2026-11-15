from pathlib import Path
import hashlib
import re
import struct
import uuid

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Resources/Fonts/Inconsolata/font.png'
SHA256 = 'dcadf257376009a61d93996a33c42459e02b3736265c5c92ade5c1db8cec78b0'


def generate():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != SHA256:
        raise ValueError('Inconsolata source checksum mismatch')
    declaration = (ROOT / 'Mu_Basecore/MdeModulePkg/MdeModulePkg.dec').read_text()
    definition = re.search(r'gConsoleFontProtocolGuid\s*=\s*\{(.*?)\}', declaration)[1]
    fields = [int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]+)', definition)]
    guid = uuid.UUID(bytes_le=struct.pack('<IHH8B', *fields))
    resources = (ROOT / 'Platforms/OnePlus/hummerPkg/Include/RAW.inc').read_text().lower()
    if f'file freeform = {guid}' not in resources:
        raise ValueError('Bitmap font resource GUID does not match its protocol')
    header = (ROOT / 'Mu_Basecore/MdeModulePkg/Include/Library/ConsoleFont.h').read_text()
    width = int(re.search(r'#define CONSOLE_FONT_WIDTH\s+(\d+)', header)[1])
    height = int(re.search(r'#define CONSOLE_FONT_HEIGHT\s+(\d+)', header)[1])
    atlas = Image.open(SOURCE).convert('L')
    if atlas.size != (96 * width, 2 * height):
        raise ValueError('Recovery font dimensions do not match the console')
    glyphs = {code: atlas.crop(((code - 32) * width, 0, (code - 31) * width, height))
              for code in range(32, 127)}
    directions = {
        0x2500: 'lr', 0x2502: 'ud', 0x250c: 'dr', 0x2510: 'dl',
        0x2514: 'ur', 0x2518: 'ul', 0x251c: 'udr', 0x2524: 'udl',
        0x252c: 'dlr', 0x2534: 'ulr', 0x253c: 'udlr',
    }
    symbols = [0x2190, 0x2191, 0x2192, 0x2193, 0x2588, 0x2591, 0x25b2, 0x25ba, 0x25bc, 0x25c4]
    x, y = width // 2, height // 2
    for code in sorted(set(directions) | set(symbols)):
        bitmap = Image.new('L', (width, height))
        draw = ImageDraw.Draw(bitmap)
        if code in directions:
            ends = {'u': (x, 0), 'd': (x, height - 1), 'l': (0, y), 'r': (width - 1, y)}
            for direction in directions[code]:
                draw.line((x, y, *ends[direction]), fill=255)
        elif code == 0x2588:
            draw.rectangle((0, 0, width - 1, height - 1), fill=255)
        elif code == 0x2591:
            for row in range(0, height, 2):
                for col in range((row // 2) % 2, width, 2):
                    bitmap.putpixel((col, row), 255)
        elif code in (0x25b2, 0x25bc, 0x25ba, 0x25c4):
            points = {
                0x25b2: [(x, y - 5), (x - 5, y + 4), (x + 5, y + 4)],
                0x25bc: [(x, y + 5), (x - 5, y - 4), (x + 5, y - 4)],
                0x25ba: [(x + 5, y), (x - 4, y - 5), (x - 4, y + 5)],
                0x25c4: [(x - 5, y), (x + 4, y - 5), (x + 4, y + 5)],
            }
            draw.polygon(points[code], fill=255)
        else:
            draw.line((x, y - 6, x, y + 6) if code in (0x2191, 0x2193) else (x - 6, y, x + 6, y), fill=255)
            tip, a, b = {
                0x2190: ((x - 6, y), (x - 2, y - 4), (x - 2, y + 4)),
                0x2191: ((x, y - 6), (x - 4, y - 2), (x + 4, y - 2)),
                0x2192: ((x + 6, y), (x + 2, y - 4), (x + 2, y + 4)),
                0x2193: ((x, y + 6), (x - 4, y + 2), (x + 4, y + 2)),
            }[code]
            draw.line((*a, *tip, *b), fill=255)
        glyphs[code] = bitmap
    glyphs[0xfffd] = glyphs[ord('?')]
    codes = sorted(glyphs)
    data = struct.pack('<8sHHHH', b'ICFONT01', width, height, len(codes), 0)
    data += struct.pack('<' + 'H' * len(codes), *codes)
    data += b''.join(glyphs[code].tobytes() for code in codes)
    output = ROOT / 'Build/font-cache/Inconsolata.bin'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    print(f'Inconsolata Regular: {len(codes)} glyphs, {width}x{height}, 8-bit alpha, {len(data)} bytes')


if __name__ == '__main__':
    generate()
