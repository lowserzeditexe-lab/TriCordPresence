#!/usr/bin/env python3
"""
gen_qr.py — génère une petite page HTML avec un QR code pointant vers
l'installeur .cia (pratique pour l'installer via FBI sans câble).

Optionnel et non-bloquant : si le paquet `qrcode` n'est pas installé, ou si
aucune URL n'est fournie, on écrit une page minimale avec juste le lien en
texte plutôt que d'échouer le build. build.sh appelle ce script avec
`|| true` justement pour ça.

Usage : gen_qr.py [URL] --out chemin/vers/qr.html
"""
import sys
import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url", nargs="?", default=None,
                         help="URL publique du .cia (ex: hébergé sur un serveur perso). "
                              "Sans URL, page texte simple générée.")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    if not args.url:
        with open(args.out, "w") as f:
            f.write("<html><body><p>Aucune URL fournie (QR_URL). "
                    "Copiez tricord-presence-installer.cia sur la SD "
                    "(/cias/) ou installez via FBI en local.</p></body></html>")
        print(f"[gen_qr] pas d'URL fournie -> page texte simple : {args.out}")
        return

    try:
        import qrcode
        import base64
        from io import BytesIO

        img = qrcode.make(args.url)
        buf = BytesIO()
        img.save(buf, format="PNG")
        b64 = base64.b64encode(buf.getvalue()).decode("ascii")
        with open(args.out, "w") as f:
            f.write(
                f"<html><body>"
                f"<p>Scanne avec FBI (3DS) : <code>{args.url}</code></p>"
                f'<img src="data:image/png;base64,{b64}" />'
                f"</body></html>"
            )
        print(f"[gen_qr] QR généré : {args.out}")
    except ImportError:
        with open(args.out, "w") as f:
            f.write(f"<html><body><p>Installe via FBI : <code>{args.url}</code></p>"
                     f"<p>(paquet Python 'qrcode' absent -> pas de QR image, "
                     f"juste le lien : `pip install qrcode[pil]` pour l'avoir)</p></body></html>")
        print("[gen_qr] paquet 'qrcode' absent -> page texte avec lien seulement")


if __name__ == "__main__":
    sys.exit(main())
