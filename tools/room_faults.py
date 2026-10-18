"""Bounded, loopback-only relays for encrypted datagrams of complete lab rooms."""
import heapq
import random
import select
import socket
import struct
import threading
import time


PROFILES = {
    "delay": {"delay_ms": 60},
    "jitter": {"delay_ms": 10, "jitter_ms": 90},
    "loss": {"burst_drop": 3, "burst_period": 10},
    "duplication": {"duplicate_every": 4},
    "reordering": {"reorder_every": 3, "reorder_extra_ms": 250},
    "blackout": {"blackout_fraction": [0, 1]},
    "bandwidth": {"bytes_per_second": 800},
    "combined": {"delay_ms": 20, "jitter_ms": 60, "burst_drop": 2, "burst_period": 12,
                 "duplicate_every": 5, "reorder_every": 4, "reorder_extra_ms": 250,
                 "blackout_fraction": [0.35, 0.65], "bytes_per_second": 4000},
}


class RoomRelay:
    """Each endpoint connects to its own proxy; the opposite proxy forwards to it.

    The controller receives only endpoint port numbers, never TLS keys/plaintext.
    Fault decisions apply after all clients are ready. One thread owns socket I/O.
    """
    QUEUE_LIMIT = 16  # encrypted datagrams per direction

    def __init__(self, profile, seed):
        self.profile = profile
        self.parameters = PROFILES[profile]
        self.seed = seed
        self.lock = threading.RLock()
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(32)
        self.port = self.listener.getsockname()[1]
        self.pairs, self.routes, self.directions, self.pending = {}, {}, [], []
        self.serial = 0
        self.start = self.end = None
        self.stopping = threading.Event()
        self.errors = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def activate(self, start, end):
        with self.lock:
            self.start, self.end = start, end

    def register(self):
        connection, address = self.listener.accept()
        with connection:
            connection.settimeout(1)
            request = b""
            while len(request) < 4:
                data = connection.recv(4 - len(request))
                if not data:
                    raise RuntimeError("Incomplete relay registration")
                request += data
            local, remote = struct.unpack("!HH", request)
            if address[0] != "127.0.0.1" or min(local, remote) < 1024 or local == remote:
                raise RuntimeError("Invalid relay endpoints")
            key = tuple(sorted((local, remote)))
            with self.lock:
                if key not in self.pairs:
                    if len(self.pairs) >= 32:
                        raise RuntimeError("Relay pair limit exceeded")
                    sides = {}
                    for endpoint in key:
                        proxy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        proxy.bind(("127.0.0.1", 0))
                        proxy.setblocking(False)
                        sides[endpoint] = proxy
                    self.pairs[key] = {"sides": sides, "registered": set()}
                    for source, destination in (key, key[::-1]):
                        direction = dict(source_port=source, destination_port=destination,
                                         received=0, forwarded=0, fault_received=0,
                                         dropped_loss=0, dropped_blackout=0, dropped_queue=0,
                                         duplicates=0, reordered=0, forwarded_out_of_order=0,
                                         queued=0, peak_queue=0, peak_queued_bytes=0, queued_bytes=0,
                                         delay_samples=0, delay_sum_ms=0.0, delay_max_ms=0.0,
                                         delay_min_ms=None)
                        state = {"stats": direction, "source": sides[source], "target": sides[destination],
                                 "destination": ("127.0.0.1", destination), "last_forwarded": 0,
                                 "next_send": 0.0, "index": 0,
                                 "random": random.Random(self.seed + len(self.directions))}
                        self.directions.append(state)
                        self.routes[sides[source]] = state
                pair = self.pairs[key]
                if local in pair["registered"]:
                    raise RuntimeError("Duplicate relay registration")
                pair["registered"].add(local)
                connection.sendall(struct.pack("!H", pair["sides"][local].getsockname()[1]))

    def enqueue(self, direction, data, now):
        stats = direction["stats"]
        stats["received"] += 1
        impaired = self.start is not None and self.start <= now < self.end
        parameters = self.parameters if impaired else {}
        if impaired:
            stats["fault_received"] += 1
            direction["index"] += 1
        index = direction["index"]
        blackout = parameters.get("blackout_fraction")
        if blackout and blackout[0] <= (now - self.start) / (self.end - self.start) < blackout[1]:
            stats["dropped_blackout"] += 1
            return
        if index and (index - 1) % parameters.get("burst_period", 1) < parameters.get("burst_drop", 0):
            stats["dropped_loss"] += 1
            return
        delay = parameters.get("delay_ms", 0) + direction["random"].uniform(0, parameters.get("jitter_ms", 0))
        if parameters.get("reorder_every") and index % parameters["reorder_every"] == 0:
            delay += parameters["reorder_extra_ms"]
            stats["reordered"] += 1
        duplicate = parameters.get("duplicate_every") and index % parameters["duplicate_every"] == 0
        for copy in range(2 if duplicate else 1):
            if stats["queued"] >= self.QUEUE_LIMIT:
                stats["dropped_queue"] += 1
                continue
            transmission = now
            rate = parameters.get("bytes_per_second")
            if rate:
                transmission = max(now, direction["next_send"]) + len(data) / rate
                direction["next_send"] = transmission
            due = transmission + delay / 1000 + copy * 0.005
            if copy:
                stats["duplicates"] += 1
            stats["queued"] += 1
            stats["queued_bytes"] += len(data)
            stats["peak_queue"] = max(stats["peak_queue"], stats["queued"])
            stats["peak_queued_bytes"] = max(stats["peak_queued_bytes"], stats["queued_bytes"])
            self.serial += 1
            heapq.heappush(self.pending, (due, self.serial, direction, data, now, stats["received"], impaired))

    def forward(self, now):
        while self.pending and self.pending[0][0] <= now:
            _, _, direction, data, received, index, impaired = heapq.heappop(self.pending)
            stats = direction["stats"]
            stats["queued"] -= 1
            stats["queued_bytes"] -= len(data)
            direction["target"].sendto(data, direction["destination"])
            stats["forwarded"] += 1
            if index < direction["last_forwarded"]:
                stats["forwarded_out_of_order"] += 1
            direction["last_forwarded"] = max(index, direction["last_forwarded"])
            if impaired:
                ms = (now - received) * 1000
                stats["delay_samples"] += 1
                stats["delay_sum_ms"] += ms
                stats["delay_max_ms"] = max(stats["delay_max_ms"], ms)
                stats["delay_min_ms"] = ms if stats["delay_min_ms"] is None else min(stats["delay_min_ms"], ms)

    def run(self):
        try:
            while not self.stopping.is_set():
                sockets = [self.listener] + list(self.routes)
                readable, _, _ = select.select(sockets, [], [], 0.005)
                for sock in readable:
                    if sock is self.listener:
                        self.register()
                        continue
                    # Bound work per socket so one client cannot starve the others.
                    for _ in range(64):
                        try:
                            data, sender = sock.recvfrom(16384)
                        except BlockingIOError:
                            break
                        except ConnectionResetError:
                            # Windows UDP ICMP after a guest's normal socket cleanup.
                            # Unexpected process exits are checked by the room runner.
                            break
                        with self.lock:
                            direction = self.routes[sock]
                            if sender != ("127.0.0.1", direction["stats"]["source_port"]):
                                raise RuntimeError("Unexpected datagram source")
                            self.enqueue(direction, data, time.monotonic())
                with self.lock:
                    self.forward(time.monotonic())
        except Exception as error:
            self.errors.append(f"{type(error).__name__}: {error}")

    def snapshot(self):
        with self.lock:
            return {"profile": self.profile, "parameters": self.parameters, "seed": self.seed,
                    "queue_limit_datagrams_per_direction": self.QUEUE_LIMIT,
                    "registered_endpoints": sum(len(p["registered"]) for p in self.pairs.values()),
                    "directions": [dict(d["stats"]) for d in self.directions], "errors": list(self.errors)}

    def close(self):
        self.stopping.set()
        self.thread.join(timeout=3)
        if self.thread.is_alive():
            raise RuntimeError("Relay thread did not stop")
        self.listener.close()
        for pair in self.pairs.values():
            for sock in pair["sides"].values():
                sock.close()


class FaultObserver:
    def __init__(self, relay, count, seconds):
        self.relay, self.count, self.seconds = relay, count, seconds
        self.origin = self.fault_start = self.fault_end = None
        self.before = self.after = None
        self.recovery_s = None
        self.progress_at = 0
        self.last_phase = None

    def tick(self, clients):
        now = time.monotonic()
        if self.relay.errors:
            raise RuntimeError("Relay failure: " + "; ".join(self.relay.errors))
        if self.origin is None:
            if not all(c.started is not None and c.active and len(c.positions) == self.count for c in clients):
                return
            self.origin = now
            self.fault_start = now + self.seconds * 0.2
            self.fault_end = now + self.seconds * 0.6
            self.relay.activate(self.fault_start, self.fault_end)
            self.before = [dict(c.positions) for c in clients]
        if now >= self.fault_end and self.after is None:
            self.after = [dict(c.positions) for c in clients]
        if self.after is not None and self.recovery_s is None:
            if all(all(c.positions[e]["revision"] > state["revision"] and
                       c.positions[e]["position"] != state["position"] for e, state in baseline.items())
                   for c, baseline in zip(clients, self.after)):
                self.recovery_s = now - self.fault_end
        elapsed = now - self.origin
        phase = "warmup" if now < self.fault_start else ("faults" if now < self.fault_end else "recovery")
        if elapsed >= self.progress_at or phase != self.last_phase:
            print(f"  {self.relay.profile}, {self.count} clients: {elapsed:.0f}/{self.seconds}s ({phase})", flush=True)
            self.progress_at = elapsed + 10
            self.last_phase = phase

    def validate(self, clients):
        snapshot = self.relay.snapshot()
        if snapshot["registered_endpoints"] != self.count * 2:
            raise RuntimeError("Not all native UDP endpoints used the relay; use dsr_net_fault_lab")
        if self.recovery_s is None or self.recovery_s > 3:
            raise RuntimeError("Not every entity recovered a changing newer state within 3 seconds")
        for direction in snapshot["directions"]:
            if not direction["fault_received"]:
                raise RuntimeError("Missing bidirectional fault traffic")
            profile = self.relay.profile
            required = {"loss": "dropped_loss", "blackout": "dropped_blackout",
                        "duplication": "duplicates", "reordering": "forwarded_out_of_order",
                        "bandwidth": "dropped_queue"}.get(profile)
            if required and not direction[required]:
                raise RuntimeError(f"Fault was not exercised in each direction: {required}")
            if profile == "combined" and any(not direction[k] for k in (
                    "dropped_loss", "dropped_blackout", "duplicates", "forwarded_out_of_order")):
                raise RuntimeError("Combined faults did not all occur bidirectionally")
            if profile in ("delay", "jitter") and not direction["delay_samples"]:
                raise RuntimeError("No delayed datagrams delivered")
            if direction["delay_samples"] and direction["delay_min_ms"] < self.relay.parameters.get("delay_ms", 0):
                raise RuntimeError("Delivered datagram violated the configured delay floor")
        # Existing Process parser checks every applied UDP revision for rollback.
        if any(c.errors for c in clients):
            raise RuntimeError("Client error or applied revision rollback")

    def report(self):
        return {**self.relay.snapshot(), "phase_origin_monotonic": self.origin,
                "fault_start_offset_s": self.seconds * 0.2, "fault_end_offset_s": self.seconds * 0.6,
                "all_entities_recovered_s": self.recovery_s, "recovery_budget_s": 3,
                "applied_revision_check": "Every UDP_POSITION must strictly advance its entity revision"}
