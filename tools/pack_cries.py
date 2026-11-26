#!/usr/bin/env python3
"""Build a TamaPoke microSD cry pack (TPAK archive of 16 kHz 4-bit IMA-ADPCM .adp files).

Usage:
  python3 tools/pack_cries.py --src /path/to/cries/pokemon/latest --out cries.pak
  python3 tools/pack_cries.py --fetch --out cries.pak
  python3 tools/pack_cries.py --self-test

Accepts <id>.ogg / <id>.wav / c<id>.wav / c<id>.ogg for dex numbers 1..1025
(matching PokeAPI/cries layout) and requires ffmpeg on PATH unless --self-test.
"""
import argparse
import ctypes
from concurrent.futures import ThreadPoolExecutor, as_completed
import glob
import math
import os
import re
import shutil
import struct
import subprocess
import tempfile
import urllib.request

CRY_SAMPLE_RATE = 16000
CRY_MAX_BYTES = 65536
IMA_INDEX_TABLE = (
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
)
IMA_STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
)


def encode_tcry(pcm_s16le: bytes, rate: int = CRY_SAMPLE_RATE) -> bytes:
    samples = struct.unpack(f'<{len(pcm_s16le) // 2}h', pcm_s16le)
    if not samples:
        raise ValueError('empty audio stream')
    pred = samples[0]
    idx = 0
    nibbles = []
    for s in samples:
        diff = s - pred
        nib = 0
        if diff < 0:
            nib = 8
            diff = -diff
        step = IMA_STEP_TABLE[idx]
        vpdiff = step >> 3
        if diff >= step:
            nib |= 4
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            nib |= 2
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            nib |= 1
            vpdiff += step
        pred = pred - vpdiff if (nib & 8) else pred + vpdiff
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IMA_INDEX_TABLE[nib]))
        nibbles.append(nib)
    body = bytearray((len(nibbles) + 1) // 2)
    for i, nib in enumerate(nibbles):
        if i & 1:
            body[i >> 1] |= (nib & 0x0F) << 4
        else:
            body[i >> 1] = nib & 0x0F
    hdr = struct.pack('<4sHhBBH', b'TCRY', rate, samples[0], 0, 0, 0)
    out = hdr + bytes(body)
    if len(out) > CRY_MAX_BYTES:
        out = out[:CRY_MAX_BYTES]
    return out


def decode_tcry(blob: bytes):
    magic, rate, pred, idx, _flags, _res = struct.unpack_from('<4sHhBBH', blob, 0)
    if magic != b'TCRY' or rate != CRY_SAMPLE_RATE or idx > 88:
        raise ValueError('invalid TCRY header')
    out = []
    for b in blob[12:]:
        for nib in (b & 0x0F, (b >> 4) & 0x0F):
            step = IMA_STEP_TABLE[idx]
            diff = step >> 3
            if nib & 1:
                diff += step >> 2
            if nib & 2:
                diff += step >> 1
            if nib & 4:
                diff += step
            pred = pred - diff if (nib & 8) else pred + diff
            pred = max(-32768, min(32767, pred))
            idx = max(0, min(88, idx + IMA_INDEX_TABLE[nib]))
            out.append(pred)
    return out


def pack_tpak(entries):
    out = bytearray(b'TPAK' + struct.pack('<H', len(entries)))
    for name, data in entries:
        nb = name.encode('ascii')
        out += struct.pack('<B', len(nb)) + nb + struct.pack('<I', len(data))
    for _name, data in entries:
        out += data
    return bytes(out)


def _decode_libsndfile(path: str) -> bytes:
    """Decode audio file to 16 kHz mono s16le using libsndfile + libsamplerate."""
    sf = ctypes.CDLL('libsndfile.so.1')
    sr = ctypes.CDLL('libsamplerate.so.0')

    class SfInfo(ctypes.Structure):
        _fields_ = [
            ('frames', ctypes.c_int64),
            ('samplerate', ctypes.c_int),
            ('channels', ctypes.c_int),
            ('format', ctypes.c_int),
            ('sections', ctypes.c_int),
            ('seekable', ctypes.c_int),
        ]

    class SrcData(ctypes.Structure):
        _fields_ = [
            ('data_in', ctypes.POINTER(ctypes.c_float)),
            ('data_out', ctypes.POINTER(ctypes.c_float)),
            ('input_frames', ctypes.c_long),
            ('output_frames', ctypes.c_long),
            ('input_frames_used', ctypes.c_long),
            ('output_frames_gen', ctypes.c_long),
            ('end_of_input', ctypes.c_int),
            ('src_ratio', ctypes.c_double),
        ]

    sf.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(SfInfo)]
    sf.sf_open.restype = ctypes.c_void_p
    sf.sf_readf_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int64]
    sf.sf_readf_float.restype = ctypes.c_int64
    sf.sf_close.argtypes = [ctypes.c_void_p]
    sr.src_simple.argtypes = [ctypes.POINTER(SrcData), ctypes.c_int, ctypes.c_int]
    sr.src_simple.restype = ctypes.c_int

    info = SfInfo()
    handle = sf.sf_open(path.encode('utf-8'), 0x10, ctypes.byref(info))
    if not handle:
        raise RuntimeError(f'libsndfile failed to open {path}')
    try:
        max_in = int(info.samplerate * 3.8) + 128
        frames = min(int(info.frames), max_in) if info.frames > 0 else max_in
        buf = (ctypes.c_float * (frames * info.channels))()
        got = sf.sf_readf_float(handle, buf, frames)
    finally:
        sf.sf_close(handle)
    if got <= 0:
        raise ValueError(f'no audio frames decoded from {path}')

    if info.channels > 1:
        mono = (ctypes.c_float * got)()
        ch = info.channels
        for i in range(got):
            s = 0.0
            base = i * ch
            for c in range(ch):
                s += buf[base + c]
            mono[i] = s / ch
    else:
        mono = buf

    if info.samplerate != CRY_SAMPLE_RATE:
        ratio = float(CRY_SAMPLE_RATE) / float(info.samplerate)
        out_cap = min(int(math.ceil(got * ratio)) + 16, int(CRY_SAMPLE_RATE * 3.8))
        out_buf = (ctypes.c_float * out_cap)()
        sd = SrcData(
            data_in=mono,
            data_out=out_buf,
            input_frames=got,
            output_frames=out_cap,
            src_ratio=ratio,
        )
        err = sr.src_simple(ctypes.byref(sd), 1, 1)  # SRC_SINC_MEDIUM_QUALITY
        if err:
            raise RuntimeError(f'libsamplerate failed ({err}) for {path}')
        samples_f = out_buf
        n = min(int(sd.output_frames_gen), int(CRY_SAMPLE_RATE * 3.8))
    else:
        samples_f = mono
        n = min(int(got), int(CRY_SAMPLE_RATE * 3.8))

    ints = [max(-32768, min(32767, int(round(samples_f[i] * 32767.0)))) for i in range(n)]
    return struct.pack(f'<{n}h', *ints)


def convert_file(path: str) -> bytes:
    if shutil.which('ffmpeg'):
        cmd = [
            'ffmpeg', '-v', 'error', '-i', path,
            '-ac', '1', '-ar', str(CRY_SAMPLE_RATE),
            '-t', '3.8', '-f', 's16le', '-',
        ]
        pcm = subprocess.check_output(cmd)
    else:
        pcm = _decode_libsndfile(path)
    return encode_tcry(pcm)


def self_test():
    n = CRY_SAMPLE_RATE // 4
    pcm = struct.pack(
        f'<{n}h',
        *[int(16000 * math.sin(2.0 * math.pi * 440.0 * i / CRY_SAMPLE_RATE)) for i in range(n)],
    )
    tcry = encode_tcry(pcm)
    assert tcry[:4] == b'TCRY' and len(tcry) == 12 + n // 2
    dec = decode_tcry(tcry)
    src = struct.unpack(f'<{n}h', pcm)
    err = sum((a - b) ** 2 for a, b in zip(src, dec)) / n
    sig = sum(a ** 2 for a in src) / n
    snr = 10.0 * math.log10(sig / max(1.0, err))
    assert snr > 25.0, f'SNR too low: {snr:.2f} dB'
    pak = pack_tpak([('cries/c025.adp', tcry)])
    assert pak[:4] == b'TPAK'
    print(f'PASS pack_cries self-test (SNR={snr:.1f} dB, {len(tcry)} B TCRY, {len(pak)} B TPAK)')


def main():
    ap = argparse.ArgumentParser(description='Build cries.pak for TamaPoke microSD')
    ap.add_argument('--src', help='Directory containing <id>.ogg/.wav files')
    ap.add_argument('--fetch', action='store_true', help='Download latest/*.ogg from PokeAPI/cries')
    ap.add_argument('--cache-dir', default=os.path.join(tempfile.gettempdir(), 'tamapoke_cries_cache'),
                    help='Cache directory for fetched .ogg files')
    ap.add_argument('--out', default='cries.pak', help='Output .pak path (default: cries.pak)')
    ap.add_argument('--max-dex', type=int, default=1025, help='Max dex id when fetching (default: 1025)')
    ap.add_argument('--self-test', action='store_true', help='Run built-in ADPCM/TPAK check')
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return

    entries = []
    if args.fetch:
        os.makedirs(args.cache_dir, exist_ok=True)

        def fetch_one(dex):
            dst = os.path.join(args.cache_dir, f'{dex}.ogg')
            if not os.path.exists(dst) or os.path.getsize(dst) == 0:
                url = f'https://raw.githubusercontent.com/PokeAPI/cries/main/cries/pokemon/latest/{dex}.ogg'
                try:
                    urllib.request.urlretrieve(url, dst)
                except Exception:
                    if os.path.exists(dst):
                        os.remove(dst)
                    return None
            return dex, convert_file(dst)

        results = {}
        with ThreadPoolExecutor(max_workers=16) as pool:
            futs = [pool.submit(fetch_one, d) for d in range(1, args.max_dex + 1)]
            for fut in as_completed(futs):
                res = fut.result()
                if res is not None:
                    dex, data = res
                    results[dex] = data
        entries = [(f'cries/c{dex:03d}.adp', results[dex]) for dex in sorted(results)]
    elif args.src:
        found = {}
        for path in sorted(glob.glob(os.path.join(args.src, '*'))):
            m = re.match(r'^c?(\d{1,4})\.(ogg|wav|mp3|flac)$', os.path.basename(path), re.I)
            if m:
                dex = int(m.group(1))
                if 1 <= dex <= 1025 and dex not in found:
                    found[dex] = path
        for dex in sorted(found):
            entries.append((f'cries/c{dex:03d}.adp', convert_file(found[dex])))
    else:
        ap.error('specify --src <dir>, --fetch, or --self-test')

    if not entries:
        raise SystemExit('no cry audio files found')
    blob = pack_tpak(entries)
    with open(args.out, 'wb') as f:
        f.write(blob)
    print(f'wrote {args.out}: {len(entries)} cries, {len(blob) / 1048576:.2f} MiB')


if __name__ == '__main__':
    main()
