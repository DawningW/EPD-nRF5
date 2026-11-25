## RLE Image Format

Album images (files under `/img/` on the littlefs filesystem) use a headerless bitstream RLE format, generalized over bits per pixel (`bpp`): 1bpp streams for black & white / black & red planes, 2bpp streams for 4-color panels. The web frontend encodes images with `encodeRLE()` (`web/src/gallery.js`), the command-line tool `tools/rle.py` (process / compress / decompress) produces byte-identical output; the firmware streams the data chunk by chunk from flash and decodes it straight into the EPD RAM (`FS/image.c`, `EPD/EPD_service.c`).

### Encoding

**Literal** — bit7 = 0: the low 7 bits hold `7/bpp` pixels, MSB first (1bpp: 7 pixels, 2bpp: 3 pixels). Unused trailing bits are zero.

```
1bpp:  |0|p|p|p|p|p|p|p|
2bpp:  |0| p | p | p |0|
```

**Run** — bit7 = 1: the pixel value `val` (`bpp` bits) repeats `count + 8` times. `cont` = 1 means the count continues in the following bytes.

```
|1| val |cont| count low |
             count low: 6-bpp bits
```

A single run byte covers up to 39 pixels (1bpp) / 23 pixels (2bpp). Continuation bytes are little-endian 7-bit groups (like a protobuf varint), low bits first: the first group shifts by `6-bpp`, each following group by 7 more; a byte with bit7 = 0 ends the count.

Runs shorter than 8 pixels are always encoded as literals — a run opcode costs 1 byte and covers at least 8 pixels, while a literal byte covers at most 7 (1bpp) / 3 (2bpp).

### Pixel Packing

Decoded pixels are packed MSB-first into bytes.

- 1bpp black & white plane: 1 = white, 0 = black
- 1bpp red plane (BWR): 0 = red, 1 = not red
- 2bpp: native panel color codes — 0 = black, 1 = white, 2 = yellow, 3 = red

### File Layout

A file is one or more streams concatenated back to back, with no header and no padding between streams. Each stream decodes exactly `width * height` pixels; the decoder truncates the trailing padding of the last literal group by pixel count.

| Panel | Streams |
|-------|---------|
| BW | one 1bpp stream |
| BWR | two 1bpp streams: black plane, then red plane (red plane optional — the image is drawn without it if missing) |
| BWRY | one 2bpp stream |

### Encoder Rules

- Runs ≥ 8 pixels become run opcodes. A pending partial literal group is first padded with pixels of the run, because the decoder always consumes whole literal groups.
- Runs < 8 pixels become literals.
- The final partial literal group is padded with zero bits.

### Examples

| Bytes | Decoded |
|-------|---------|
| `C2` (1bpp) | 10 × white |
| `FC 02` (1bpp) | 100 × white |
| `00` (1bpp) | 7 × black (truncated to the expected pixel count) |
| `E4` (2bpp) | 12 × red |

### Command-Line Tool

`tools/rle.py` creates images without the web frontend, or converts them back to images or raw pixel data. It requires `opencv-python` and `numpy`.

```bash
python tools/rle.py process input.png --width 400 --height 300 --mode bwry -o out.rle
python tools/rle.py compress out.bmp --width 400 --height 300 --mode bwry -o out.rle
python tools/rle.py decompress out.rle --width 400 --height 300 --mode bwry -o out.bmp
```

- `process`: scales an image to the given size (letterbox) and converts it to panel colors
- `compress`: compresses an image (already `--width` × `--height`) or raw pixel data to RLE
- `decompress`: decompresses RLE to an image or raw pixel data

`compress` and `decompress` treat `.bmp` / `.png` / `.jpg` files as images, anything else as raw pixel data. `--mode` is `bw` / `bwr` / `bwry`; see `python tools/rle.py <command> -h` for all options.
