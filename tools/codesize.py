#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


def load_sizes(size_dir):
    # each <target>.txt holds the output of `arm-none-eabi-size <target>.out`:
    #    text    data     bss     dec     hex filename
    #   51236    1476    5012   57724    e17c _build/nrf52811_xxaa.out
    sizes = {}
    for path in sorted(Path(size_dir).glob('*.txt')):
        for line in path.read_text().splitlines():
            fields = line.split()
            if len(fields) >= 6 and fields[0].isdigit():
                sizes[path.stem] = {
                    'text': int(fields[0]),
                    'data': int(fields[1]),
                    'bss': int(fields[2]),
                }
                break
        else:
            sys.exit('error: no size data in %s' % path)
    if not sizes:
        sys.exit('error: no size files in %s' % size_dir)
    return sizes


def load_baseline(path):
    with open(path) as f:
        return json.load(f)


def flash(size):
    return size['text'] + size['data']


def ram(size):
    return size['data'] + size['bss']


def delta(cur, base):
    d = cur - base
    return '+%d' % d if d > 0 else str(d)


def render(current, baseline=None):
    header = ['target', 'text', 'data', 'bss', 'flash', 'ram']
    if baseline is not None:
        header += ['d-flash', 'd-ram']
    lines = [
        '| ' + ' | '.join(header) + ' |',
        '|' + ' --- |' * len(header),
    ]
    for target in sorted(current):
        size = current[target]
        row = [target, size['text'], size['data'], size['bss'],
               flash(size), ram(size)]
        if baseline is not None:
            base = baseline.get(target)
            if base:
                row += [delta(flash(size), flash(base)),
                        delta(ram(size), ram(base))]
            else:
                row += ['new', 'new']
        lines.append('| ' + ' | '.join(str(v) for v in row) + ' |')
    return '\n'.join(lines)


def cmd_write(args):
    with open(args.baseline, 'w') as f:
        json.dump(load_sizes(args.size_dir), f, indent=2, sort_keys=True)
        f.write('\n')


def cmd_show(args):
    baseline = load_baseline(args.baseline) if args.baseline else None
    print(render(load_sizes(args.size_dir), baseline))


def cmd_diff(args):
    old = load_baseline(args.old) if args.old else None
    print(render(load_baseline(args.new), old))


def main():
    parser = argparse.ArgumentParser(description='Firmware code size report tool')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('write', help='write baseline json from size files')
    p.add_argument('size_dir')
    p.add_argument('baseline')
    p.set_defaults(func=cmd_write)
    p = sub.add_parser('show', help='print size table, with deltas if baseline given')
    p.add_argument('size_dir')
    p.add_argument('-b', '--baseline')
    p.set_defaults(func=cmd_show)
    p = sub.add_parser('diff', help='print baseline diff table')
    p.add_argument('new')
    p.add_argument('--old')
    p.set_defaults(func=cmd_diff)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
