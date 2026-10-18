"""Generate a private lab identity in a NEW directory, without a Windows store.

Requires an OpenSSL CLI (e.g. Git for Windows). No application TLS library
dependency, no certificate installation, and no overwrite of existing output.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import ssl
import subprocess


def find_openssl():
    candidate = shutil.which("openssl")
    if candidate:
        return candidate
    for candidate in (
        Path(r"C:\Program Files\Git\usr\bin\openssl.exe"),
        Path(r"C:\Program Files\Git\mingw64\bin\openssl.exe"),
    ):
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("OpenSSL CLI not found; provide --openssl PATH")


def create_identity(output, openssl):
    password = os.environ.get("DSR_TLS_PFX_PASSWORD", "")
    if not (12 <= len(password) <= 128 and all(32 <= ord(c) <= 126 for c in password)):
        raise RuntimeError("Defina DSR_TLS_PFX_PASSWORD com 12 a 128 caracteres ASCII")
    directory = Path(output).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    key = directory / "host.key.pem"
    cert = directory / "host.crt.pem"
    pfx = directory / "host.pfx"
    config = Path(__file__).with_name("localhost.cnf")
    commands = [
        [openssl, "req", "-x509", "-newkey", "rsa:3072", "-sha256", "-days", "30",
         "-config", str(config), "-keyout", str(key), "-out", str(cert),
         "-passout", "env:DSR_TLS_PFX_PASSWORD"],
        [openssl, "pkcs12", "-export", "-in", str(cert), "-inkey", str(key), "-out", str(pfx),
         "-name", "DSR Network Lab", "-keypbe", "AES-256-CBC", "-certpbe", "AES-256-CBC",
         "-macalg", "sha256", "-passin", "env:DSR_TLS_PFX_PASSWORD", "-passout", "env:DSR_TLS_PFX_PASSWORD"],
    ]
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            # Avoid leaking environment secrets or arbitrary tool diagnostics.
            raise RuntimeError("OpenSSL failed; the partial directory was preserved. Choose another directory before trying again.")
    der = ssl.PEM_cert_to_DER_cert(cert.read_text(encoding="ascii"))
    return pfx, cert, hashlib.sha256(der).hexdigest()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="Pasta NOVA para identidade privada; evite pastas sincronizadas")
    parser.add_argument("--openssl", help="Caminho do openssl.exe")
    args = parser.parse_args()
    try:
        pfx, cert, pin = create_identity(args.output, args.openssl or find_openssl())
        print("PFX:", pfx)
        print("Certificado publico:", cert)
        print("TLS_PIN:", pin)
        print("Keep the PFX and private key on the host; share only the pin through a trusted channel.")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(1, str(error) + "\n")
