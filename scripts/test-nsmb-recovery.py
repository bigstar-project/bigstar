"""Real two-emulator recovery test using a previously prepared replay fixture.

Only child processes and loopback traffic created by this test are affected.
The fixture contains host/client environment.json, game.nds, game.sav,
melonDS.toml and replay.inputs (the recording-replay preparation output).
"""
import argparse
import csv
import ctypes
import json
import os
from pathlib import Path
import re
import select
import selectors
import shutil
import socket
import subprocess
import threading
import time
import urllib.request
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]
FIELDS = [f"playerActor{i}{field}" for i in (0, 1) for field in ("X", "Y", "VelX", "VelY")]
FIELDS += ["netRandomValue", "netRandomCallCount"]
FIELDS += [f"input{kind}{i}{edge}" for kind in ("Console", "Player") for i in (0, 1) for edge in ("Held", "Pressed")]


def udp_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    return sock


class Proxy:
    def __init__(self, host_port, trace_path):
        self.front, self.back = udp_socket(), udp_socket()
        self.host = ("127.0.0.1", host_port)
        self.client = None
        self.drop = "none"
        self.stopped = threading.Event()
        self.dropped = 0
        self.trace = open(trace_path, "w", encoding="utf-8")
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        with selectors.DefaultSelector() as selector:
            selector.register(self.front, selectors.EVENT_READ)
            selector.register(self.back, selectors.EVENT_READ)
            while not self.stopped.is_set():
                for key, _ in selector.select(0.02):
                    payload, source = key.fileobj.recvfrom(65536)
                    to_host = key.fileobj is self.front
                    self.trace.write(json.dumps({"unix_ms": time.time_ns() // 1000000,
                        "to": "host" if to_host else "client", "drop": self.drop,
                        "hex": payload.hex()}) + "\n")
                    if to_host:
                        self.client = source
                    if self.drop == "both" or self.drop == ("host" if to_host else "client"):
                        self.dropped += 1
                        continue
                    target = self.host if to_host else self.client
                    if target:
                        (self.back if to_host else self.front).sendto(payload, target)

    def close(self):
        self.stopped.set()
        self.thread.join(2)
        self.front.close()
        self.back.close()
        self.trace.close()


class SignalProxy:
    """Break only this test host's real signaling TCP connection."""
    def __init__(self, port):
        self.target = ("127.0.0.1", port)
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.drop = threading.Event()
        self.stopped = threading.Event()
        self.workers = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def relay(self, incoming):
        with incoming:
            if self.drop.is_set(): return
            try:
                with socket.create_connection(self.target, timeout=2) as outgoing:
                    incoming.settimeout(1)
                    while not self.stopped.is_set() and not self.drop.is_set():
                        ready, _, _ = select.select([incoming, outgoing], [], [], 0.05)
                        for source in ready:
                            data = source.recv(65536)
                            if not data: return
                            (outgoing if source is incoming else incoming).sendall(data)
            except OSError:
                pass  # Deliberate disconnects also reset the sockets.

    def run(self):
        while not self.stopped.is_set():
            try:
                incoming, _ = self.listener.accept()
            except socket.timeout:
                continue
            worker = threading.Thread(target=self.relay, args=(incoming,), daemon=True)
            self.workers.append(worker)
            worker.start()

    def close(self):
        self.stopped.set()
        self.thread.join(3)
        self.listener.close()
        for worker in self.workers: worker.join(3)


def suspend(child, enabled):
    fn = ctypes.windll.ntdll.NtSuspendProcess if enabled else ctypes.windll.ntdll.NtResumeProcess
    fn.argtypes = [ctypes.c_void_p]
    fn.restype = ctypes.c_long
    if fn(int(child._handle)) != 0:
        raise RuntimeError("could not suspend/resume test child")


def verify(output, expect_timeout, rollback=False, outages=()):
    result = {"generations": [], "roles": {}}
    for role in ("host", "client"):
        text = (output / role / "stdout.txt").read_text(encoding="utf-8", errors="replace")
        result["roles"][role] = {
            "disconnected": text.count("peer disconnected"),
            "verified": text.count("NSMB Recovery: peer verified"),
            "resumed": text.count("NSMB Recovery: resumed"),
            "deadline_exceeded": "reason=deadline-exceeded" in text,
            "horizon_timeout": "reason=prediction-horizon-timeout" in text,
            "frame_limit": "frame limit reached" in text,
            "critical": len(re.findall(r"game state mismatch[^\n]+playerGlobal=0\s", text)),
        }
    for path in sorted((output / "host").glob("game-tick-g*.csv")):
        peer = output / "client" / path.name
        if not peer.exists():
            raise AssertionError(f"missing peer trace: {peer}")
        def read(p):
            with p.open(encoding="utf-8", newline="") as handle:
                rows = list(csv.DictReader(handle))
            frames = [int(row["frame"]) for row in rows]
            if frames != sorted(set(frames)):
                raise AssertionError(f"duplicate or reversed game ticks: {p}")
            # ROM-loop replay does not visit the checkpoint gate on every
            # tick. Require coverage around the actual outage below instead.
            if not rollback and frames and frames != list(range(frames[0], frames[-1] + 1)):
                raise AssertionError(f"noncontiguous game ticks: {p}")
            return {int(row["frame"]): tuple(row[k] for k in FIELDS) for row in rows}
        host, client = read(path), read(peer)
        common = sorted(host.keys() & client.keys())
        differences = [frame for frame in common if host[frame] != client[frame]]
        result["generations"].append({"trace": path.name, "common_ticks": len(common),
            "first_tick": common[0] if common else None, "last_tick": common[-1] if common else None,
            "mismatches": len(differences), "first_differences": differences[:10]})
        for outage in outages:
            if path.name != f"game-tick-g{outage['generation']}.csv":
                continue
            frame = outage["frame"]
            # Compare both the interruption and at least 200 subsequent ticks;
            # a startup-only trace must never certify successful recovery.
            before = sum(frame - 100 <= tick <= frame for tick in common)
            after = sum(frame < tick <= frame + 300 for tick in common)
            outage["compared_before"] = before
            outage["compared_after"] = after
    result["ok"] = bool(result["generations"]) and all(g["common_ticks"] > 0 and g["mismatches"] == 0 for g in result["generations"])
    # Display-frame hashes include speculative state in rollback mode. Its
    # comparison is the confirmed pre-input gate trace, rewritten by replay.
    if not rollback:
        result["ok"] &= all(v["critical"] == 0 for v in result["roles"].values())
    if expect_timeout:
        result["ok"] &= all(v["deadline_exceeded"] or v["horizon_timeout"] for v in result["roles"].values())
    else:
        result["ok"] &= all(v["frame_limit"] and v["resumed"] > 0 for v in result["roles"].values())
        result["ok"] &= all(o.get("compared_before", 0) >= 90 and
                            o.get("compared_after", 0) >= 200 for o in outages)
    result["outages"] = list(outages)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/release-windows-x86_64/melonDS.exe")
    parser.add_argument("--mode", choices=("udp", "stall-host", "stall-client", "webrtc", "signaling"), default="udp")
    parser.add_argument("--bridge", type=Path, default=ROOT / "tools/bigstar-net-bridge/target/debug/bigstar-net-bridge.exe")
    parser.add_argument("--signal", default="http://127.0.0.1:18888")
    parser.add_argument("--direction", choices=("both", "host", "client"), default="both")
    parser.add_argument("--outage", type=float, default=10)
    parser.add_argument("--trigger-frame", type=int, default=1500)
    parser.add_argument("--trigger-generation", type=int, default=0)
    parser.add_argument("--frames", type=int, default=5000)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--expect-timeout", action="store_true")
    parser.add_argument("--rollback", action="store_true", help="Use the production D=2/P=7 ROM-loop settings")
    args = parser.parse_args()
    if args.direction != "both" and args.mode != "udp":
        parser.error("--direction applies only to --mode udp")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    with udp_socket() as reservation:
        host_port = reservation.getsockname()[1]
    proxy = Proxy(host_port, output / "enet-packets.jsonl")
    proxy.thread.start()
    children, bridges, handles = {}, {}, []
    outage_file = output / "outage"
    client_port = proxy.front.getsockname()[1]
    paused = None
    signaling_proxy = None
    injected = False
    injections = 0
    outages = []
    start = time.monotonic()
    try:
        if args.mode in ("webrtc", "signaling"):
            if urllib.parse.urlparse(args.signal).hostname not in ("127.0.0.1", "localhost"):
                raise ValueError("recovery tests require a local signaling server")
            def post(path, data):
                req = urllib.request.Request(args.signal + path, json.dumps(data).encode(),
                                             {"content-type": "application/json"}, method="POST")
                with urllib.request.urlopen(req, timeout=10) as response:
                    return json.load(response)
            identity = {key: str(i) * 64 for i, key in enumerate(("host_rom_sha256", "client_rom_sha256",
                         "generator_id", "rom_pair_id", "bridge_sha256", "save_sha256"), 1)}
            fixture_env = json.loads((args.fixture / "host/environment.json").read_text(encoding="utf-8"))
            room = post("/rooms", {"host_name": "Recovery test", "rom_identity": identity,
                "settings": {"big_stars": int(fixture_env["MELONDS_NSML_MVL_BIG_STARS"]),
                             "course_mode": fixture_env["MELONDS_NSML_MVL_COURSE_MODE"],
                             "course_stages": [int(v) for v in fixture_env["MELONDS_NSML_MVL_STAGE_SEQUENCE"].split(',')],
                             "lives": fixture_env["MELONDS_NSML_MVL_LIVES"],
                             "match_seed": fixture_env["MELONDS_NSML_MATCH_SEED"],
                             "rng_seeds": fixture_env["MELONDS_NSML_MATCH_SEED_SEQUENCE"].split(','),
                             "wins": int(fixture_env["MELONDS_NSML_MVL_WINS"]),
                             "input_delay_frames": 2 if args.rollback else 3,
                             "input_max_frame_lead": 0 if args.rollback else 4,
                             "rollback_enabled": args.rollback}})
            joined = post(f'/rooms/{room["room_id"]}/join', {"rom_pair_id": identity["rom_pair_id"]})
            if args.mode == "signaling":
                signaling_proxy = SignalProxy(urllib.parse.urlparse(args.signal).port or 80)
            with udp_socket() as reservation:
                client_port = reservation.getsockname()[1]
            for role, token in (("host", room["host_token"]), ("client", joined["join_token"])):
                log_dir = output / f"bridge-{role}"
                log_dir.mkdir()
                out, err = open(log_dir / "stdout.txt", "wb"), open(log_dir / "stderr.txt", "wb")
                handles.extend((out, err))
                signal_url = room["signal_url"]
                if signaling_proxy and role == "host":
                    signal_url = urllib.parse.urlparse(signal_url)._replace(netloc=f"127.0.0.1:{signaling_proxy.port}").geturl()
                command = [str(args.bridge.resolve()), "webrtc-offer" if role == "host" else "webrtc-answer",
                    "--local-bind", "127.0.0.1:0" if role == "host" else f"127.0.0.1:{client_port}",
                    "--signal", signal_url + "?token=" + token, "--session", room["room_id"],
                    "--status-file", str(log_dir / "status.json")]
                if role == "host": command += ["--local-target", f"127.0.0.1:{proxy.front.getsockname()[1]}"]
                env = dict(os.environ)
                env["BIGSTAR_DETAILED_LOGS"] = "1"
                if role == "host": env["BIGSTAR_NET_BRIDGE_OUTAGE_FILE"] = str(outage_file)
                bridges[role] = subprocess.Popen(command, env=env, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
            while True:
                phases = []
                for role, child in bridges.items():
                    if child.poll() is not None: raise AssertionError(f"{role} bridge exited before connection")
                    try: phases.append(json.loads((output / f"bridge-{role}/status.json").read_text())["phase"])
                    except (FileNotFoundError, json.JSONDecodeError): pass
                if phases == ["connected", "connected"]: break
                if time.monotonic() - start > 50: raise TimeoutError("WebRTC initial connection")
                time.sleep(0.1)
        for role in ("host", "client"):
            source, dest = args.fixture.resolve() / role, output / role
            dest.mkdir()
            for name in ("game.nds", "game.sav", "melonDS.toml", "replay.inputs"):
                shutil.copy2(source / name, dest / name)
            shutil.copy2(args.exe.resolve(), dest / "melonDS.exe")
            env = {k: v for k, v in os.environ.items() if not k.startswith("MELONDS_")}
            config = json.loads((source / "environment.json").read_text(encoding="utf-8"))
            for key, value in config.items():
                env[key] = value.replace(str(source), str(dest))
            env.update({"MELONDS_NSML_PORT": str(host_port if role == "host" else client_port),
                        "MELONDS_NSML_TEST_FRAMES": str(args.frames), "MELONDS_NSML_WAIT_TIMEOUT_MS": "60000",
                        "MELONDS_NSML_GAME_TICK_TRACE": str(dest / "game-tick"),
                        "MELONDS_NSML_RECOVERY_STATUS": str(dest / "recovery.json")})
            env["MELONDS_NSML_ENET_TRACE"] = "1"
            env["QT_QPA_PLATFORM"] = "offscreen"
            if args.rollback:
                env.update({"MELONDS_NSML_ROLLBACK": "1", "MELONDS_NSML_ROLLBACK_BACKEND": "romloop",
                    "MELONDS_NSML_ROLLBACK_WINDOW": "16", "MELONDS_NSML_ROLLBACK_CHECKPOINT_INTERVAL": "1",
                    "MELONDS_NSML_ROLLBACK_RESIMULATE": "1", "MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES": "7",
                    "MELONDS_NSML_ROLLBACK_PREDICTION_HORIZON_FRAMES": "7",
                    "MELONDS_NSML_ROLLBACK_HORIZON_TIMEOUT_MS": "60000",
                    "MELONDS_NSML_ROLLBACK_PHASE_RECOVERY": "1", "MELONDS_NSML_FIXED_FRAME_SLEEP": "1",
                    "MELONDS_NSML_ROM_GAME_TICK_PROBE_GAME_RAM_ROLLBACK": "1",
                    "MELONDS_NSML_ROM_GAME_TICK_PROBE_DEFER_LCD": "1",
                    "MELONDS_NSML_ROM_GAME_TICK_PROBE_REPLAY_RENDER": "1",
                    "MELONDS_NSML_ROM_GAME_TICK_PROBE_DISCARD_INTERMEDIATE_3D": "1",
                    "MELONDS_NSML_JIT_EXACT_BLOCK_CHAIN": "1",
                    "MELONDS_NSML_JIT_EXACT_BLOCK_CHAIN_ALLOW_ROM_PROBE": "1",
                    "MELONDS_NSML_JIT_SELF_LOOP_FAST_PATH": "1",
                    "MELONDS_NSML_INPUT_MAX_FRAME_LEAD": "0", "MELONDS_NSML_INPUT_BUNDLE_HISTORY": "12",
                    "MELONDS_NSML_DELAY": "2"})
            (dest / "environment.json").write_text(json.dumps({k: v for k, v in env.items() if k.startswith("MELONDS_")}, indent=2))
            stdout, stderr = open(dest / "stdout.txt", "wb"), open(dest / "stderr.txt", "wb")
            handles.extend((stdout, stderr))
            children[role] = subprocess.Popen([str(dest / "melonDS.exe"), str(dest / "game.nds")],
                cwd=dest, env=env, stdout=stdout, stderr=stderr,
                creationflags=subprocess.CREATE_NO_WINDOW)
        while any(child.poll() is None for child in children.values()):
            now = time.monotonic()
            if now - start > args.outage * args.repeat + 120:
                raise TimeoutError("bounded recovery test exceeded wall clock limit")
            if not injected and injections < args.repeat:
                log = (output / "host/stdout.txt").read_text(encoding="utf-8", errors="replace")
                generations = list(re.finditer(r"received start ready generation=(\d+)", log))
                frames = re.findall(r"sent input[^\n]*?frame=(\d+)", log[generations[-1].end():]) if generations else []
                if (frames and generations and int(generations[-1][1]) == args.trigger_generation
                        and int(frames[-1]) >= args.trigger_frame + injections * 800):
                    injected = True
                    injections += 1
                    outages.append({"generation": args.trigger_generation, "frame": int(frames[-1])})
                    resume_at = now + args.outage
                    if args.mode == "udp": proxy.drop = args.direction
                    elif args.mode == "webrtc": outage_file.touch()
                    elif args.mode == "signaling": signaling_proxy.drop.set()
                    else:
                        paused = children[args.mode.removeprefix("stall-")]
                        suspend(paused, True)
                    print(f"injected {args.mode} at input {frames[-1]} for {args.outage}s", flush=True)
            elif injected and now >= resume_at:
                proxy.drop = "none"
                if signaling_proxy: signaling_proxy.drop.clear()
                if outage_file.exists(): outage_file.unlink()
                if paused:
                    suspend(paused, False)
                    paused = None
                resume_at = float("inf")
                injected = False
                print("outage ended", flush=True)
            time.sleep(0.01)
        if injections != args.repeat:
            raise AssertionError("test finished without injecting every outage")
    finally:
        if paused: suspend(paused, False)
        for child in [*children.values(), *bridges.values()]:
            if child.poll() is None: child.terminate()
            child.wait(timeout=10)
        for handle in handles: handle.close()
        proxy.close()
        if signaling_proxy: signaling_proxy.close()
    result = verify(output, args.expect_timeout, args.rollback, outages)
    result.update({"mode": args.mode, "outage_seconds": args.outage, "dropped_datagrams": proxy.dropped,
                   "exit_codes": {role: child.returncode for role, child in children.items()},
                   "elapsed_seconds": round(time.monotonic() - start, 2)})
    if not args.expect_timeout: result["ok"] &= all(c.returncode == 0 for c in children.values())
    else: result["ok"] &= all(c.returncode == 70 for c in children.values())
    if args.mode in ("webrtc", "signaling") and not args.expect_timeout:
        result["webrtc_connections"] = {role: (output / f"bridge-{role}/stdout.txt").read_text(encoding="utf-8", errors="replace").count("recovery: connected connectionId=") for role in bridges}
        result["ok"] &= all(count >= args.repeat + 1 for count in result["webrtc_connections"].values())
    (output / "verification.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
