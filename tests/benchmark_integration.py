"""Validate automated trajectories and reports with all supported lab counts."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE, OPENSSL = sys.argv[1:3]
del sys.argv[1:3]
BENCHMARK = Path(__file__).resolve().parents[1] / "tools" / "movement_benchmark.py"


class BenchmarkTests(unittest.TestCase):
    def test_automatic_2_4_8_and_report(self):
        with tempfile.TemporaryDirectory(prefix="dsr-benchmark-report-") as temp:
            output = Path(temp) / "report.json"
            result = subprocess.run([sys.executable, str(BENCHMARK), "--exe", EXE,
                                     "--openssl", OPENSSL, "--seconds", "4",
                                     "--output", str(output)], capture_output=True, text=True, timeout=75)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, (result.stdout, result.stderr, report))
            self.assertEqual([s["clients"] for s in report["scenarios"]], [2, 4, 8])
            self.assertEqual(report["schema"], 2)
            for scenario in report["scenarios"]:
                self.assertTrue(scenario["passed"])
                self.assertGreater(scenario["simultaneous_overlap_s"], 2)
                self.assertEqual(len(scenario["process_runs"]), scenario["clients"] + 1)
                for process in scenario["process_runs"]:
                    self.assertFalse(process["errors"])
                    self.assertGreaterEqual(len(process["memory_samples"]), 3)
                    for field in ("working_set_bytes", "private_bytes"):
                        summary = process["memory_summary"][field]
                        self.assertGreater(summary["sampled_peak"], 0)
                        self.assertGreaterEqual(summary["sampled_peak"], summary["last"])
                    summary = process["metrics_summary"]
                    self.assertFalse(any(summary["failures"].values()))
                    self.assertGreater(summary["control_queue_peak_bytes"], 0)
                    self.assertEqual(sum(summary["update_interval_histogram"]), summary["update_interval_samples"])
                    self.assertEqual(summary["final_connections"], scenario["clients"] if process["role"] == "host" else 1)
                for run in scenario["client_runs"]:
                    metric = run["metrics"][-1]
                    self.assertTrue(metric["final"])
                    self.assertGreater(metric["update_interval_samples"], 0)
                    self.assertGreater(metric["update_interval_max_ms"], 0)
                    self.assertGreaterEqual(metric["tcp_rtt_samples"], 2)

    def test_failed_launch_preserves_diagnostics(self):
        with tempfile.TemporaryDirectory(prefix="dsr-benchmark-failure-") as temp:
            # An executable that exists (can be hashed), but rejects the host CLI.
            output = Path(temp) / "failure.json"
            result = subprocess.run([sys.executable, str(BENCHMARK), "--exe", sys.executable,
                                     "--openssl", OPENSSL, "--seconds", "4", "--clients", "2",
                                     "--output", str(output)], capture_output=True, text=True, timeout=25)
            self.assertNotEqual(result.returncode, 0)
            scenario = json.loads(output.read_text(encoding="utf-8"))["scenarios"][0]
            self.assertFalse(scenario["passed"])
            self.assertTrue(scenario["errors"])
            self.assertIsNotNone(scenario["process_runs"][0]["exit_code"])

    def test_automatic_options_reject_invalid_combinations(self):
        for args in (["host", "--auto-seconds", "5"], ["join", "--auto-seconds", "0"],
                     ["join", "--auto-seconds", "3601"], ["join", "--auto-seconds"],
                     ["join", "--auto-seconds", "5"], ["host", "--metrics"]):
            result = subprocess.run([EXE] + args, capture_output=True, text=True, timeout=5)
            self.assertNotEqual(result.returncode, 0, args)
            self.assertNotIn("SERVER_READY", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
