"""Exercise complete native rooms under loopback encrypted-UDP network faults."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import secrets
import tempfile
from unittest.mock import patch

from create_tls_identity import create_identity, find_openssl
from movement_benchmark import scenario
from room_faults import PROFILES, RoomRelay, FaultObserver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build-v04/dsr_net_fault_lab.exe")
    parser.add_argument("--seconds", type=int, default=15)
    parser.add_argument("--clients", type=int, nargs="+", default=[2, 4, 8])
    parser.add_argument("--profiles", nargs="+", choices=list(PROFILES), default=list(PROFILES))
    parser.add_argument("--seed", type=int, default=305419896)
    parser.add_argument("--sample-seconds", type=float, default=1)
    parser.add_argument("--output", required=True)
    parser.add_argument("--openssl")
    parser.add_argument("--tls-library")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 3600 or any(n not in (2, 4, 8) for n in args.clients):
        parser.error("Use 10-3600 seconds and client counts 2, 4, 8")
    if not 0.1 <= args.sample_seconds <= 10:
        parser.error("Use a memory sample interval of 0.1-10 seconds")
    output = Path(args.output)
    if output.exists():
        parser.error("Choose a new report path; existing reports are preserved")
    exe = str(Path(args.exe).resolve())
    report = {"schema": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
              "platform": platform.platform(), "python": platform.python_version(),
              "executable_sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
              "scope": "Complete native host and clients; only encrypted UDP datagrams impaired, TCP/TLS unchanged",
              "transport": "loopback TCP/TLS + UDP/DTLS via per-connection relays",
              "seed": args.seed, "memory_sample_seconds": args.sample_seconds,
              "update_interval_bucket_upper_ms": [20, 50, 100, 200, 500, 1000, 2000, 5000, None],
              "timing": "Per-room clean warmup 20%, faults 40%, clean recovery 40%; no cross-PC clocks",
              "scenarios": [], "passed": False}
    total = len(args.profiles) * len(args.clients)
    with tempfile.TemporaryDirectory(prefix="dsr-room-faults-") as directory:
        env = dict(os.environ, DSR_ROOM_PASSWORD=secrets.token_hex(16),
                   DSR_TLS_PFX_PASSWORD=secrets.token_hex(16))
        with patch.dict(os.environ, DSR_TLS_PFX_PASSWORD=env["DSR_TLS_PFX_PASSWORD"]):
            pfx, _, pin = create_identity(Path(directory) / "identity", args.openssl or find_openssl())
        for profile in args.profiles:
            for count in args.clients:
                index = len(report["scenarios"]) + 1
                print(f"[{index}/{total}] Starting {profile}: {count} clients, {args.seconds}s", flush=True)
                relay = RoomRelay(profile, args.seed)
                observer = FaultObserver(relay, count, args.seconds)
                try:
                    result = scenario(exe, count, args.seconds, pfx, pin,
                                      dict(env, DSR_LAB_RELAY_PORT=str(relay.port)), args.tls_library,
                                      args.sample_seconds, observer)
                finally:
                    relay.close()
                # Include the final drained/cleanup state of the relay too.
                result["network_faults"] = observer.report()
                if relay.errors:
                    result["passed"] = False
                    result["errors"].extend(relay.errors)
                report["scenarios"].append(result)
                report["passed"] = len(report["scenarios"]) == total and all(s["passed"] for s in report["scenarios"])
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                print(f"[{index}/{total}] {profile}, {count} clients: {'PASS' if result['passed'] else 'FAIL'}"
                      f"; recovery={observer.recovery_s}", flush=True)
                for error in result["errors"]:
                    print(f"  {error}", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
