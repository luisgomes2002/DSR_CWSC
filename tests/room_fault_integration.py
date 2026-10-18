"""Full native rooms must recover after bidirectional encrypted UDP faults."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE, OPENSSL, PRODUCTION_EXE = sys.argv[1:4]
del sys.argv[1:4]
RUNNER = Path(__file__).resolve().parents[1] / "tools" / "room_fault_benchmark.py"


class RoomFaultTests(unittest.TestCase):
    def run_matrix(self, extra, exe=EXE):
        with tempfile.TemporaryDirectory(prefix="dsr-room-fault-test-") as temp:
            path = Path(temp) / "report.json"
            result = subprocess.run([sys.executable, str(RUNNER), "--exe", exe,
                                     "--openssl", OPENSSL, "--seconds", "12",
                                     "--output", str(path)] + extra,
                                    capture_output=True, text=True, timeout=190)
            return result, json.loads(path.read_text(encoding="utf-8"))

    def test_combined_faults_complete_rooms_2_4_8(self):
        result, report = self.run_matrix(["--profiles", "combined"])
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        self.assertTrue(report["passed"])
        self.assertEqual([s["clients"] for s in report["scenarios"]], [2, 4, 8])
        for scenario in report["scenarios"]:
            self.assertTrue(scenario["passed"], scenario["errors"])
            faults = scenario["network_faults"]
            self.assertEqual(faults["registered_endpoints"], 2 * scenario["clients"])
            self.assertLessEqual(faults["all_entities_recovered_s"], 3)
            for direction in faults["directions"]:
                for counter in ("dropped_loss", "dropped_blackout", "duplicates", "forwarded_out_of_order"):
                    self.assertGreater(direction[counter], 0, counter)
                self.assertLessEqual(direction["peak_queue"], faults["queue_limit_datagrams_per_direction"])
            for process in scenario["process_runs"]:
                self.assertGreater(len(process["memory_samples"]), 3)
                self.assertFalse(process["errors"])

    def test_blackout_and_bandwidth_recover(self):
        result, report = self.run_matrix(["--clients", "2", "--profiles", "blackout", "bandwidth"])
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        self.assertTrue(report["passed"])
        blackout, bandwidth = report["scenarios"]
        self.assertGreater(max(p["metrics_summary"]["update_interval_max_ms"]
                               for p in blackout["process_runs"]), 4000)
        for direction in bandwidth["network_faults"]["directions"]:
            self.assertGreater(direction["dropped_queue"], 0)

    def test_production_executable_cannot_silently_bypass_faults(self):
        result, report = self.run_matrix(["--clients", "2", "--profiles", "loss"], PRODUCTION_EXE)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report["passed"])
        self.assertEqual(report["scenarios"][0]["network_faults"]["registered_endpoints"], 0)
        self.assertTrue(report["scenarios"][0]["errors"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
