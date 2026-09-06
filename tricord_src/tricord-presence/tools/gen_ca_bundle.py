#!/usr/bin/env python3
"""Génère sysmodule/source/discord_ca_bundle.h : racines CA (PEM) acceptées
pour gateway.discord.gg, embarquées dans le sysmodule pour activer la
vérification du certificat mbedtls (MBEDTLS_SSL_VERIFY_REQUIRED).

Chaîne observée (openssl s_client, juin 2026) : discord.gg <- WE1 <- GTS Root
R4 <- GlobalSign Root CA. Discord/Cloudflare alternent historiquement entre
Google Trust Services, DigiCert (Cloudflare Inc ECC CA-3 <- Baltimore) et
Let's Encrypt (ISRG Root X1) : on embarque ces racines-là.

Source des PEM : magasin système (ca-certificates Debian, /etc/ssl/certs),
ou un dossier passé en argument contenant les .pem.
"""
import os, sys

ROOTS = [
    "GTS_Root_R1", "GTS_Root_R2", "GTS_Root_R3", "GTS_Root_R4",
    "GlobalSign_Root_CA", "ISRG_Root_X1",
    "DigiCert_Global_Root_CA", "DigiCert_Global_Root_G2", "Baltimore_CyberTrust_Root",
]

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "/etc/ssl/certs"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "sysmodule", "source", "discord_ca_bundle.h")
    lines = ["// Généré par tools/gen_ca_bundle.py — ne pas éditer à la main.",
             "// Racines CA acceptées pour gateway.discord.gg (voir RAPPORT.md §TLS).",
             "#pragma once", "static const char DISCORD_CA_BUNDLE[] ="]
    found = []
    for name in ROOTS:
        path = os.path.join(src, name + ".pem")
        if not os.path.exists(path):
            print(f"!! {name}.pem absent de {src}", file=sys.stderr); continue
        pem = open(path).read().strip()
        lines.append(f"    /* {name.replace('_', ' ')} */")
        lines.extend(f'    "{l}\\n"' for l in pem.splitlines())
        found.append(name)
    lines.append(";")
    open(out, "w").write("\n".join(lines) + "\n")
    print(f"{len(found)} racines -> {out}")

if __name__ == "__main__":
    main()
