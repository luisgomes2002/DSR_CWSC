"""Reproducible synthetic movement benchmark, loopback only; no game required."""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import queue
import secrets
import subprocess
import tempfile
import threading
import time
from unittest.mock import patch

from create_tls_identity import create_identity, find_openssl


class MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
            "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


def process_memory(process):
    """Sample only our child process, using its existing Windows process handle."""
    counters = MemoryCounters()
    counters.cb = ctypes.sizeof(counters)
    api = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
    api.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    api.restype = wintypes.BOOL
    if not api(wintypes.HANDLE(int(process._handle)), ctypes.byref(counters), counters.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return {"working_set_bytes": counters.WorkingSetSize,
            "private_bytes": counters.PrivateUsage,
            "os_peak_working_set_bytes": counters.PeakWorkingSetSize}


def summarize(metrics):
    """Sum each connection's last counters, retaining partial evidence on failure."""
    finals = [m for m in metrics if m["final"]]
    latest = list({m["player"]: m for m in metrics}.values())
    histogram = [sum(m["update_interval_histogram"][i] for m in latest) for i in range(9)]
    samples = sum(m["update_interval_samples"] for m in latest)
    return {
        "final_connections": len(finals),
        "failures": {key: sum(m[key] for m in latest) for key in (
            "dtls_send_failures", "dtls_send_timeouts", "control_queue_rejections")},
        "control_queue_peak_bytes": max((m["control_queue_peak_bytes"] for m in metrics), default=0),
        "dtls_peak_pending_bytes": max((m["dtls_peak_pending_bytes"] for m in metrics), default=0),
        "update_interval_max_ms": max((m["update_interval_max_ms"] for m in metrics), default=0),
        "observed_silence_max_ms": max((m["current_silence_max_ms"] for m in metrics), default=0),
        "update_interval_histogram": histogram,
        "update_interval_samples": samples,
        "update_interval_mean_ms": sum(m["update_interval_mean_ms"] * m["update_interval_samples"]
                                       for m in latest) / samples if samples else None,
    }


class Process:
    def __init__(self, args, env):
        self.process = subprocess.Popen(args, env=env, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        # Automatic clients must finish their duration even with stdin at EOF.
        self.process.stdin.close()
        self.events = queue.Queue()
        self.metrics, self.errors, self.positions = [], [], {}
        self.memory = []
        self.sample_origin = time.monotonic()
        self.started = self.ended = None
        self.active = False
        self.threads = [threading.Thread(target=self.read, daemon=True),
                        threading.Thread(target=self.read_errors, daemon=True)]
        for thread in self.threads:
            thread.start()

    def read_errors(self):
        for line in self.process.stderr:
            self.error(line.rstrip())

    def error(self, message):
        # Bound diagnostics even if a failed process floods stderr.
        if len(self.errors) < 100:
            self.errors.append(message)

    def sample_memory(self):
        if self.process.poll() is None:
            try:
                self.memory.append({"elapsed_s": time.monotonic() - self.sample_origin,
                                    **process_memory(self.process)})
            except OSError as error:
                if self.process.poll() is None:
                    self.error(f"Memory sampling failed: {error}")

    def read(self):
        for raw in self.process.stdout:
            line = raw.rstrip()
            if line.startswith("METRICS\t"):
                try:
                    metric = json.loads(line.split("\t", 1)[1])
                    required = ("player", "final", "udp_payload_sent", "udp_payload_received",
                                "payload_sent_bytes", "payload_received_bytes", "tcp_rtt_samples",
                                "control_queue_peak_bytes", "control_queue_rejections",
                                "dtls_send_failures", "dtls_send_timeouts", "dtls_peak_pending_bytes",
                                "update_interval_samples", "update_interval_mean_ms",
                                "update_interval_max_ms", "current_silence_max_ms")
                    if not isinstance(metric, dict) or any(key not in metric for key in required):
                        raise ValueError("Missing sustained-benchmark fields; rebuild the executable")
                    histogram = metric.get("update_interval_histogram")
                    if not isinstance(histogram, list) or len(histogram) != 9:
                        raise ValueError("Invalid interval histogram")
                    self.metrics.append(metric)
                except (ValueError, TypeError) as error:
                    self.error(f"Invalid metrics: {error}")
            elif line.startswith("UDP_POSITION\t"):
                f = line.split("\t")
                entity, revision = int(f[1]), int(f[5])
                previous = self.positions.get(entity)
                if previous and revision <= previous["revision"]:
                    self.error("Non-increasing applied revision")
                self.positions[entity] = {
                    "revision": revision, "samples": (previous["samples"] if previous else 0) + 1,
                    "changed": (previous["changed"] if previous else False)
                               or bool(previous and previous["position"] != f[2:5]),
                    "position": f[2:5],
                }
            elif line.startswith("AUTO_START\t"):
                self.started = time.monotonic()
            elif line == "BYE":
                self.ended = time.monotonic()
            elif line.startswith("UDP_ACTIVE\t"):
                self.active = True
            elif line.startswith("ERROR\t"):
                self.error(line)
            elif line.startswith(("SERVER_READY\t", "TLS_PIN\t")):
                self.events.put(line)

    def wait(self, timeout):
        code = self.process.wait(timeout=timeout)
        for thread in self.threads:
            thread.join(timeout=3)
        return code

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.wait(3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.wait(3)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


def scenario(exe, count, seconds, pfx, pin, env, library, sample_seconds=1.0, observer=None):
    processes = []
    result = {"clients": count, "duration_s": seconds, "passed": False, "errors": []}
    extra = ["--tls-library", library] if library else []
    try:
        host = Process([exe, "host", "--port", "0", "--max-players", str(count),
                        "--udp", "--metrics", "--tls-pfx", str(pfx)] + extra, env)
        processes.append(host)
        host.sample_memory()
        ready = host.events.get(timeout=10).split("\t")
        if ready[0] != "SERVER_READY" or host.events.get(timeout=5) != "TLS_PIN\t" + pin:
            raise RuntimeError("Invalid host readiness/pin")
        clients = []
        for i in range(count):
            client = Process([exe, "join", "--port", ready[2], "--name", f"auto{i}",
                              "--udp", "--metrics", "--tls-pin", pin,
                              "--auto-seconds", str(seconds)] + extra, env)
            clients.append(client)
            processes.append(client)
            client.sample_memory()
        deadline = time.monotonic() + seconds + 25
        next_sample = 0.0
        while any(c.process.poll() is None for c in clients):
            now = time.monotonic()
            if now >= next_sample:
                for process in processes:
                    process.sample_memory()
                next_sample = now + sample_seconds
            if observer is not None:
                observer.tick(clients)
            if host.process.poll() is not None:
                raise RuntimeError("Host exited during workload")
            for client in clients:
                if client.process.poll() is not None and client.process.returncode != 0:
                    raise RuntimeError("Automatic client returned failure")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("Workload deadline exceeded")
            time.sleep(min(0.05 if observer is not None else sample_seconds, remaining))
        for client in clients:
            if client.wait(max(1, deadline - time.monotonic())) != 0:
                raise RuntimeError("Automatic client returned failure")
            if client.errors or not client.active or client.started is None or client.ended is None:
                raise RuntimeError("Client did not complete a healthy automatic run")
            finals = [m for m in client.metrics if m["final"]]
            if len(finals) != 1 or finals[0]["tcp_rtt_samples"] < 2:
                raise RuntimeError("Missing final report or RTT samples")
            if len(client.positions) != count or not all(p["changed"] for p in client.positions.values()):
                raise RuntimeError("Not all trajectories were observed changing")
            if client.ended - client.started < seconds - 0.25:
                raise RuntimeError("Client ended before the requested duration")
        overlap = min(c.ended for c in clients) - max(c.started for c in clients)
        if overlap < seconds / 2:
            raise RuntimeError("Insufficient simultaneous-client overlap")
        # Wait for host to process all QUITs and emit each connection's final metrics.
        deadline = time.monotonic() + 5
        while sum(m["final"] for m in host.metrics) < count and time.monotonic() < deadline:
            time.sleep(0.05)
        if sum(m["final"] for m in host.metrics) != count or host.errors or host.process.poll() is not None:
            raise RuntimeError("Host failure or missing connection reports")
        for m in host.metrics + [m for c in clients for m in c.metrics]:
            if m["payload_sent_bytes"] != m["udp_payload_sent"] * 68 or m["payload_received_bytes"] != m["udp_payload_received"] * 68:
                raise RuntimeError("Inconsistent payload accounting")
            if any(m[key] for key in ("dtls_send_failures", "dtls_send_timeouts", "control_queue_rejections")):
                raise RuntimeError("Transport or control queue failure recorded")
        if any(p.errors or not p.memory for p in processes):
            raise RuntimeError("Missing memory samples or process diagnostics recorded")
        if observer is not None:
            observer.validate(clients)
        result.update(passed=True, simultaneous_overlap_s=overlap)
    except (RuntimeError, OSError, subprocess.TimeoutExpired, queue.Empty) as error:
        result["errors"].append(str(error) or type(error).__name__)
    finally:
        for process in reversed(processes):
            process.close()
        if observer is not None:
            result["network_faults"] = observer.report()
        result["host_metrics"] = processes[0].metrics if processes else []
        result["client_runs"] = [{"metrics": p.metrics, "positions": p.positions,
                                  "errors": p.errors, "exit_code": p.process.returncode}
                                 for p in processes[1:]]
        if processes:
            result["errors"].extend(processes[0].errors)
        result["process_runs"] = []
        for index, process in enumerate(processes):
            memory = process.memory
            result["process_runs"].append({
                "role": "host" if index == 0 else "client", "client_index": index - 1 if index else None,
                "pid": process.process.pid, "exit_code": process.process.returncode,
                "termination": "benchmark_cleanup" if index == 0 and result["passed"] else
                               ("completed" if process.ended else "failed_or_cleanup"),
                "errors": process.errors, "memory_samples": memory,
                "memory_summary": {key: {
                    "first": memory[0][key], "last": memory[-1][key],
                    "sampled_peak": max(m[key] for m in memory),
                    "last_minus_first": memory[-1][key] - memory[0][key],
                } for key in ("working_set_bytes", "private_bytes")} if memory else {},
                "metrics_summary": summarize(process.metrics),
            })
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build-v04/dsr_net.exe")
    parser.add_argument("--seconds", type=int, default=300)
    parser.add_argument("--sample-seconds", type=float, default=1.0)
    parser.add_argument("--clients", type=int, nargs="+", default=[2, 4, 8])
    parser.add_argument("--output", required=True)
    parser.add_argument("--openssl")
    parser.add_argument("--tls-library")
    args = parser.parse_args()
    if not 4 <= args.seconds <= 3600 or any(n not in (2, 4, 8) for n in args.clients):
        parser.error("Use 4-3600 seconds and client counts 2, 4, 8")
    if not 0.1 <= args.sample_seconds <= 10:
        parser.error("Use a memory sample interval of 0.1-10 seconds")
    output = Path(args.output)
    if output.exists():
        parser.error("Choose a new report path; existing reports are preserved")
    exe = str(Path(args.exe).resolve())
    openssl = args.openssl or find_openssl()
    report = {"schema": 2, "created_utc": datetime.now(timezone.utc).isoformat(),
              "memory_sample_seconds": args.sample_seconds,
              "memory_scope": "Windows working set and private committed bytes per child process; not leak proof",
              "control_queue_scope": "Application TCP output queue after movement-link creation; excludes TLS/socket buffers",
              "update_interval_bucket_upper_ms": [20, 50, 100, 200, 500, 1000, 2000, 5000, None],
              "platform": platform.platform(), "python": platform.python_version(),
              "executable_sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
              "transport": "loopback TCP/TLS + UDP/DTLS", "trajectory": "circle-v1, radius=10, omega=0.8rad/s, y=2sin(phase/2), phase=player_id",
              "rtt_channel": "TCP/TLS PING/PONG (includes local scheduling and send queue)",
              "traffic_scope": "68-byte decoded/encoded movement payloads only; excludes TLS/DTLS/IP/UDP/TCP overhead, handshakes and control",
              "scenarios": []}
    with tempfile.TemporaryDirectory(prefix="dsr-movement-benchmark-") as directory:
        env = dict(os.environ, DSR_ROOM_PASSWORD=secrets.token_hex(16),
                   DSR_TLS_PFX_PASSWORD=secrets.token_hex(16))
        with patch.dict(os.environ, DSR_TLS_PFX_PASSWORD=env["DSR_TLS_PFX_PASSWORD"]):
            pfx, _, pin = create_identity(Path(directory) / "identity", openssl)
        for count in args.clients:
            result = scenario(exe, count, args.seconds, pfx, pin, env, args.tls_library, args.sample_seconds)
            report["scenarios"].append(result)
            print(f"{count} clients: {'PASS' if result['passed'] else 'FAIL'}", flush=True)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if all(s["passed"] for s in report["scenarios"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
