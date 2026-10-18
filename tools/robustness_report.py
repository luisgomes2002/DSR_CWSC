"""Run the isolated DTLS fault suite and archive parameters, counters and results."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build-v04/dtls_unit.exe")
    parser.add_argument("--output", required=True)
    parser.add_argument("--stress-seconds", type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.stress_seconds <= 60:
        parser.error("Use 1-60 stress seconds")
    output = Path(args.output)
    if output.exists():
        parser.error("Choose a new report path; existing reports are preserved")
    exe = Path(args.exe).resolve()
    result = subprocess.run([str(exe), "--stress-seconds", str(args.stress_seconds)],
                            capture_output=True, text=True, timeout=args.stress_seconds + 90)
    report = {"schema": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
              "platform": platform.platform(), "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "transport": "isolated loopback DTLS pairs with encrypted datagram relays",
              "scope": "transport/replica tests; eight independent pairs are not an eight-client native room benchmark",
              "passed": result.returncode == 0, "exit_code": result.returncode,
              "tests_passed": [], "diagnostics": result.stderr.splitlines(),
              "network_profiles": [], "backpressure": []}
    metrics = []
    for line in result.stdout.splitlines():
        if line.startswith("PASS "):
            report["tests_passed"].append(line[5:])
        elif line.startswith("METRICS\t"):
            metrics.append(json.loads(line.split("\t", 1)[1]))
        elif line.startswith("NETWORK_PROFILE\t"):
            profile = json.loads(line.split("\t", 1)[1])
            profile["connection_metrics"] = metrics
            metrics = []
            report["network_profiles"].append(profile)
        elif line.startswith("BACKPRESSURE\t"):
            report["backpressure"].append(json.loads(line.split("\t", 1)[1]))
    # The archive is successful only if it contains the complete expected suite.
    complete = (len(report["tests_passed"]) == 22 and len(report["network_profiles"]) == 8
                and len(report["backpressure"]) == 6 and not metrics)
    report["passed"] = report["passed"] and complete
    if not complete:
        report["diagnostics"].append("Incomplete robustness report")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if report['passed'] else 'FAIL'}: {len(report['tests_passed'])} DTLS cases, "
          f"{len(report['network_profiles'])} network profiles, {len(report['backpressure'])} send-fault scenarios")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
