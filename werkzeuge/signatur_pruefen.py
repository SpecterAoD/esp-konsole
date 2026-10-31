#!/usr/bin/env python3
"""Prüft die Signatur einer signierten ESP-Konsole-Firmware (Format des ESP8266-Kerns: Daten | Signatur | u32 Länge).

Aufruf:  signatur_pruefen.py firmware.signed.bin include/signatur_public.h   (oder public.pem)
Rückgabe 0 = Signatur gültig. Braucht openssl. Ändert nichts.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile


def oeffentlicher_schluessel(pfad):
    text = open(pfad, encoding="utf-8").read()
    m = re.search(r"-----BEGIN PUBLIC KEY-----.*?-----END PUBLIC KEY-----", text, re.S)
    if not m:
        sys.exit(f"kein öffentlicher Schlüssel in {pfad}")
    return m.group(0) + "\n"


def pruefen(firmware, schluessel_pfad):
    roh = open(firmware, "rb").read()
    if len(roh) < 8:
        return False, "Datei zu kurz"
    n = struct.unpack("<L", roh[-4:])[0]
    if not 64 <= n <= 1024 or n + 4 >= len(roh):
        return False, f"keine Signatur angehängt (Länge {n})"
    daten, sig = roh[:-(n + 4)], roh[-(n + 4):-4]
    with tempfile.TemporaryDirectory() as d:
        pub, s, b = (os.path.join(d, x) for x in ("pub.pem", "sig", "bin"))
        open(pub, "w").write(oeffentlicher_schluessel(schluessel_pfad))
        open(s, "wb").write(sig)
        open(b, "wb").write(daten)
        r = subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", s, b],
                           capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr).strip()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ok, meldung = pruefen(sys.argv[1], sys.argv[2])
    print(("Signatur gültig: " if ok else "Signatur UNGÜLTIG: ") + meldung)
    sys.exit(0 if ok else 1)
