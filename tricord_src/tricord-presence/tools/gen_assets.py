#!/usr/bin/env python3
"""Génère les assets *placeholder* minimalistes requis techniquement par un
.cia (icône SMDH 48x48, bannière 256x128, audio de bannière) : aplats unis,
sans dépendance externe (PNG écrit à la main via zlib, WAV silencieux).
Pas d'UI graphique : ce sont juste des fichiers obligatoires pour makerom /
bannertool, à remplacer dans une itération future."""
import struct, sys, zlib, os

def png(path, w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

def wav(path, seconds=0.5, rate=8000):
    n = int(seconds * rate)
    pcm = b"\x00\x00" * n
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    open(path, "wb").write(hdr + b"data" + struct.pack("<I", len(pcm)) + pcm)

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "installer", "assets")
    os.makedirs(out, exist_ok=True)
    blurple = (88, 101, 242)
    png(os.path.join(out, "icon.png"), 48, 48, blurple)
    png(os.path.join(out, "banner.png"), 256, 128, blurple)
    wav(os.path.join(out, "silence.wav"))
    print("assets placeholder ->", out)
