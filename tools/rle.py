#!/usr/bin/env python3
"""EPD 相册 RLE 图片编解码工具 (格式见 docs/rle.md)

用法:
  python tools/rle.py process input.png -o out.rle --width 400 --height 300 [--mode bwr] [--preview]
  python tools/rle.py compress out.bmp --width 400 --height 300 --mode bwr -o out.rle
  python tools/rle.py decompress out.rle --width 400 --height 300 --mode bwr -o out.bmp
"""

import argparse
import sys
from pathlib import Path

import cv2
import numpy as np

# 模式 -> (bpp, 流数)
MODES = {'bw': (1, 1), 'bwr': (1, 2), 'bwry': (2, 1)}
IMG_EXT = ('.bmp', '.png', '.jpg', '.jpeg')

# 2bpp 码位 0=黑 1=白 2=黄 3=红
CODE_COLORS = np.array([[0, 0, 0], [255, 255, 255], [0, 255, 255], [0, 0, 255]], dtype=np.uint8)  # BGR
FOURCOLOR_RGB = np.array([[0, 0, 0], [255, 255, 255], [255, 255, 0], [255, 0, 0]], dtype=np.float64)
LAB_WEIGHTS = np.array([0.2, 3.0, 3.0])


def rle_encode(px, bpp):
    """像素码数组 -> RLE 字节流"""
    bounds = np.concatenate(([0], np.flatnonzero(px[1:] != px[:-1]) + 1, [len(px)]))
    runs = zip(px[bounds[:-1]].tolist(), np.diff(bounds).tolist())

    out = bytearray()
    ppb = 7 // bpp
    cbits = 6 - bpp
    lit_n = 0
    lit_byte = 0

    def push_literal(v):
        nonlocal lit_n, lit_byte
        lit_byte |= v << (7 - bpp * (lit_n + 1))
        lit_n += 1
        if lit_n == ppb:
            out.append(lit_byte)
            lit_n = 0
            lit_byte = 0

    for val, run in runs:
        if run >= 8:
            while lit_n:
                push_literal(val)
                run -= 1
            if run >= 8:
                count = run - 8
                low = count & ((1 << cbits) - 1)
                count >>= cbits
                out.append(0x80 | (val << (cbits + 1)) | ((count > 0) << cbits) | low)
                while count > 0:
                    part = count & 0x7F
                    count >>= 7
                    out.append((0x80 if count > 0 else 0) | part)
                continue
        for _ in range(run):
            push_literal(val)

    if lit_n > 0:
        out.append(lit_byte)
    return bytes(out)


def rle_decode(data, offset, expected, bpp):
    """从 data[offset:] 解一条流, 返回 (像素码数组, 消耗字节数)"""
    cbits = 6 - bpp
    mask = (1 << bpp) - 1
    px = bytearray()
    i = offset

    while len(px) < expected:
        b = data[i]
        i += 1
        if not b & 0x80:
            px.extend((b >> (7 - bpp * k)) & mask for k in range(1, 7 // bpp + 1))
            continue
        val = (b >> (cbits + 1)) & mask
        has_next = (b >> cbits) & 1
        count = b & ((1 << cbits) - 1)
        shift = cbits
        while has_next:
            b = data[i]
            i += 1
            count |= (b & 0x7F) << shift
            has_next = b >> 7
            shift += 7
        px.extend([val] * min(count + 8, expected - len(px)))

    del px[expected:]
    return np.frombuffer(bytes(px), np.uint8), i - offset


def pack_pixels(px, bpp):
    bits = ((px[:, None].astype(np.uint8) >> np.arange(bpp - 1, -1, -1)) & 1).astype(np.uint8).ravel()
    return np.packbits(bits).tobytes()


def unpack_pixels(data, total, bpp):
    bits = np.unpackbits(np.frombuffer(data, np.uint8), bitorder='big')[:total * bpp]
    return (bits.reshape(-1, bpp) @ np.array([1 << i for i in range(bpp - 1, -1, -1)], np.uint8)).astype(np.uint8)


def letterbox(img, size, color):
    """保持纵横比缩放到 size=(w,h), 空白处填充 color"""
    old_shape = img.shape[:2]
    radio = min(size[1] / old_shape[0], size[0] / old_shape[1])
    paddings = int(round(old_shape[1] * radio)), int(round(old_shape[0] * radio))
    dw, dh = size[0] - paddings[0], size[1] - paddings[1]
    dw /= 2
    dh /= 2
    if old_shape[::-1] != paddings:
        img = cv2.resize(img, paddings, interpolation=cv2.INTER_LINEAR)
    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
    return cv2.copyMakeBorder(img, top, bottom, left, right, cv2.BORDER_CONSTANT, value=color)


def rgb_to_lab(rgb):
    """rgb: (..., 3) float 0-255, 顺序 RGB"""
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92) * 100.0
    xyz = c @ np.array([[0.4124, 0.2126, 0.0193],
                        [0.3576, 0.7152, 0.1192],
                        [0.1805, 0.0722, 0.9505]])
    xyz /= np.array([95.047, 100.0, 108.883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16.0 / 116.0)
    lab = np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], axis=-1)
    return lab


FOURCOLOR_LAB = rgb_to_lab(FOURCOLOR_RGB)


def image_to_streams(img, mode, threshold, red_threshold):
    """BGR 图像 -> 每条流的像素码数组"""
    b, g, r = (img[:, :, i].astype(np.int32) for i in range(3))
    gray = np.round(0.299 * r + 0.587 * g + 0.114 * b).astype(np.int32)

    if mode == 'bw':
        return [(gray >= threshold).astype(np.uint8).ravel()]
    if mode == 'bwr':
        black = (gray >= threshold).astype(np.uint8).ravel()
        # 红色平面: 0=红, 其余 1
        red = (~((r > red_threshold) & (r > g) & (r > b))).astype(np.uint8).ravel()
        return [black, red]
    lab = rgb_to_lab(img[:, :, ::-1].astype(np.float64).reshape(-1, 3))
    dist = ((lab[:, None, :] - FOURCOLOR_LAB[None, :, :]) ** 2 * LAB_WEIGHTS).sum(-1)
    return [np.argmin(dist, axis=1).astype(np.uint8)]


def render_preview(streams, mode, width, height):
    planes = [s.reshape(height, width) for s in streams]
    if mode == 'bwry':
        return CODE_COLORS[planes[0]]
    img = np.stack([planes[0]] * 3, axis=-1) * 255
    if len(planes) > 1:
        img[planes[1] == 0] = (0, 0, 255)
    return img


def save_rle(args, streams, bpp):
    """逐流编码, round-trip 校验后写入 .rle"""
    data = bytearray()
    for codes in streams:
        enc = rle_encode(codes, bpp)
        decoded, _ = rle_decode(enc, 0, len(codes), bpp)
        if not np.array_equal(decoded, codes):
            sys.exit('校验失败: 解压数据与原始不一致')
        data += enc

    out = args.output or str(Path(args.input).with_suffix('.rle'))
    Path(out).write_bytes(data)
    raw_len = (args.width * args.height * bpp + 7) // 8 * len(streams)
    print(f"校验通过, 已保存到 '{out}': {raw_len} -> {len(data)} 字节 ({len(data) / raw_len:.1%})")


def cmd_process(args):
    img = cv2.imread(args.input, cv2.IMREAD_COLOR)
    if img is None:
        sys.exit(f'无法读取图像: {args.input}')
    fill = (0, 0, 0) if args.fill == 'black' else (255, 255, 255)
    img = letterbox(img, (args.width, args.height), fill)
    streams = image_to_streams(img, args.mode, args.threshold, args.red_threshold)
    save_rle(args, streams, MODES[args.mode][0])

    if args.preview is not None:
        path = args.preview or str(Path(args.input).with_suffix('.bmp'))
        cv2.imwrite(path, render_preview(streams, args.mode, args.width, args.height))
        print(f"预览已保存到 '{path}'")


def cmd_compress(args):
    bpp, planes = MODES[args.mode]
    stream_bytes = (args.width * args.height * bpp + 7) // 8

    if Path(args.input).suffix.lower() in IMG_EXT:
        img = cv2.imread(args.input, cv2.IMREAD_COLOR)
        if img is None or img.shape[:2] != (args.height, args.width):
            sys.exit(f'无法读取图像或尺寸不是 {args.width}x{args.height}: {args.input}')
        streams = image_to_streams(img, args.mode, args.threshold, args.red_threshold)
    else:
        raw = Path(args.input).read_bytes()
        if len(raw) == stream_bytes:
            planes = 1  # bwr 红色平面可省
        elif len(raw) != stream_bytes * planes:
            sys.exit(f'输入 {len(raw)} 字节与 {args.mode} 模式 {args.width}x{args.height} 不符, 期望 {stream_bytes * planes} 字节'
                     + ('' if planes == 1 else f' 或 {stream_bytes} 字节 (仅黑白平面)'))
        streams = [unpack_pixels(raw[p * stream_bytes:], args.width * args.height, bpp) for p in range(planes)]
    save_rle(args, streams, bpp)


def cmd_decompress(args):
    data = Path(args.input).read_bytes()
    bpp, planes = MODES[args.mode]
    total = args.width * args.height

    streams = []
    offset = 0
    for p in range(planes):
        if p and offset == len(data):
            break  # bwr 红色平面可省
        try:
            codes, used = rle_decode(data, offset, total, bpp)
        except IndexError:
            sys.exit('RLE 数据不完整')
        streams.append(codes)
        offset += used
    if offset != len(data):
        sys.exit(f'RLE 数据多余 {len(data) - offset} 字节 (检查 --mode/--width/--height 是否正确)')

    raw = b''.join(pack_pixels(codes, bpp) for codes in streams)
    out = args.output or str(Path(args.input).with_suffix('.bmp'))
    if Path(out).suffix.lower() in IMG_EXT:
        cv2.imwrite(out, render_preview(streams, args.mode, args.width, args.height))
    else:
        Path(out).write_bytes(raw)
    print(f"已保存到 '{out}': {len(data)} -> {len(raw)} 字节, {len(streams)} 条流")


def add_common(parser):
    parser.add_argument('--width', type=int, default=400, help='宽度像素 (默认 400)')
    parser.add_argument('--height', type=int, default=300, help='高度像素 (默认 300)')
    parser.add_argument('--mode', choices=MODES, default='bw', help='颜色模式: bw 黑白 / bwr 黑白+红 / bwry 四色 (默认 bw)')
    parser.add_argument('--threshold', type=int, default=140, help='黑白二值化阈值 (默认 140, 同 web 端)')
    parser.add_argument('--red-threshold', type=int, default=160, help='bwr 模式红色检测阈值 (默认 160)')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)

    p = sub.add_parser('process', help='处理图像并压缩: letterbox 缩放 + 二值化/量化 + RLE')
    p.add_argument('input', help='输入图像 (png/jpg/bmp ...)')
    p.add_argument('-o', '--output', help='输出 RLE 文件 (默认 输入名.rle)')
    add_common(p)
    p.add_argument('--fill', choices=['black', 'white'], default='black', help='letterbox 空白填充色 (默认 black)')
    p.add_argument('--preview', nargs='?', const='', default=None, metavar='IMG',
                   help='另存处理后的图像 (不带值则默认 输入名.bmp)')
    p.set_defaults(func=cmd_process)

    p = sub.add_parser('compress', help='压缩: 图像或原始像素数据 -> RLE')
    p.add_argument('input', help='输入图像 (bmp/png/jpg) 或原始像素数据 (像素码连续打包, 每像素 MSB 在前), 按后缀区分')
    p.add_argument('-o', '--output', help='输出 RLE 文件 (默认 输入名.rle)')
    add_common(p)
    p.set_defaults(func=cmd_compress)

    p = sub.add_parser('decompress', help='解压缩: RLE -> 图像或原始像素数据')
    p.add_argument('input', help='输入 RLE 文件')
    p.add_argument('-o', '--output', help='输出图像 (bmp/png/jpg) 或原始像素数据, 按后缀区分 (默认 输入名.bmp)')
    add_common(p)
    p.set_defaults(func=cmd_decompress)

    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
