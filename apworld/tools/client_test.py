"""End-to-end test of the mod's Archipelago client against a real Archipelago server.

Generates a two-player Twilight Princess Dusklight multiworld with the APWorld, hosts it with
Archipelago's MultiServer, and drives two copies of the mod's network client
(build/ap_client_driver: src/archipelago/ap_client.cpp + ws_codec.cpp on POSIX sockets).

    python client_test.py <Archipelago dir> <ap_client_driver> [--python <python with websockets>]

Checks: address forms and fallbacks, wrong slot / password, slot data, sending checks to the
other player, receiving items (live and replayed on reconnect), PrintJSON, DeathLink, goal.
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
import zipfile

GAME = "Twilight Princess Dusklight"


class Driver:
    def __init__(self, path: str, name: str):
        self.name = name
        self.proc = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.events: "queue.Queue[dict]" = queue.Queue()
        self.log: list[dict] = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            self.events.put(event)

    def send(self, line: str):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def mark(self) -> int:
        """Position in the event log; wait(since=...) also accepts events logged after it."""
        self.drain(0.05)
        return len(self.log)

    def wait(self, predicate, timeout=20.0, what="event", since=None):
        if since is not None:
            for event in self.log[since:]:
                if predicate(event):
                    return event
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                event = self.events.get(timeout=max(0.01, deadline - time.time()))
            except queue.Empty:
                break
            self.log.append(event)
            if predicate(event):
                return event
        tail = "\n".join(json.dumps(e)[:300] for e in self.log[-15:])
        raise AssertionError(f"{self.name}: timed out waiting for {what}. Last events:\n{tail}")

    def drain(self, seconds=0.5):
        end = time.time() + seconds
        while time.time() < end:
            try:
                self.log.append(self.events.get(timeout=0.05))
            except queue.Empty:
                pass

    def close(self):
        try:
            self.send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def generate(ap_dir: str, python: str, tmp: str) -> str:
    players = os.path.join(tmp, "players")
    os.makedirs(players)
    for i in (1, 2):
        with open(os.path.join(players, f"p{i}.yaml"), "w") as f:
            f.write(f"name: Link{i}\ngame: {GAME}\n{GAME}:\n  death_link: true\n")
    out = os.path.join(tmp, "out")
    script = ("import sys; sys.path.insert(0, '.'); import ModuleUpdate; ModuleUpdate.update_ran = True\n"
              "import Generate; from Main import main\n"
              "erargs, seed = Generate.main(); main(erargs, seed)\n")
    subprocess.run([python, "-c", script, "--player_files_path", players, "--outputpath", out,
                    "--seed", "4242"], cwd=ap_dir, check=True, stdout=subprocess.DEVNULL)
    archive = next(os.path.join(out, f) for f in os.listdir(out) if f.endswith(".zip"))
    with zipfile.ZipFile(archive) as z:
        name = next(n for n in z.namelist() if n.endswith(".archipelago"))
        z.extract(name, out)
    return os.path.join(out, name)


def start_server(args, multidata: str, port: int) -> subprocess.Popen:
    # Skip Archipelago's interactive requirements check (ModuleUpdate)
    script = ("import sys, runpy; sys.path.insert(0, '.'); import ModuleUpdate; ModuleUpdate.update_ran = True\n"
              "sys.argv = ['MultiServer.py'] + sys.argv[1:]\n"
              "runpy.run_path('MultiServer.py', run_name='__main__')\n")
    log = open(os.path.join(os.path.dirname(multidata), f"server_{time.time():.0f}.log"), "w")
    return subprocess.Popen([args.python, "-c", script, multidata, "--host", "127.0.0.1", "--port", str(port),
                             "--password", "secret", "--disable_save", "--loglevel", "info"],
                            cwd=args.ap_dir, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ap_dir")
    parser.add_argument("driver")
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        t = time.time()
        multidata = generate(args.ap_dir, args.python, tmp)
        print(f"generated multiworld in {time.time() - t:.1f}s")

        port = free_port()
        server = start_server(args, multidata, port)
        drivers: list[Driver] = []
        try:
            deadline = time.time() + 30
            while time.time() < deadline:
                with socket.socket() as s:
                    if s.connect_ex(("127.0.0.1", port)) == 0:
                        break
                time.sleep(0.2)
            else:
                raise AssertionError("MultiServer did not start")

            p1 = Driver(args.driver, "Link1")
            p2 = Driver(args.driver, "Link2")
            drivers += [p1, p2]

            # Address validation and refusals
            p1.send("connect-once bad/host Link1 secret")
            p1.wait(lambda e: e.get("event") == "state" and e["state"] == "Not connected"
                    and "not a valid" in e["detail"], what="invalid address")
            p1.send(f"connect-once localhost:{port} Nobody secret")
            p1.wait(lambda e: e.get("event") == "state" and "no player named" in e.get("detail", ""),
                    what="InvalidSlot refusal")
            p1.send(f"connect-once localhost:{port} Link1 wrong")
            p1.wait(lambda e: e.get("event") == "state" and "Wrong password" in e.get("detail", ""),
                    what="InvalidPassword refusal")
            print("ok: refusals reported (invalid address, unknown slot, wrong password)")

            # Non-local name: wss is tried first (unsupported in the driver), then ws
            host_ip = socket.gethostbyname(socket.gethostname())
            p2.send(f"connect {host_ip if host_ip != '127.0.0.1' else '127.0.0.1'}:{port} Link2 secret")
            c2 = p2.wait(lambda e: e.get("event") == "connected", what="Link2 connected")
            p1.send(f"connect ws://localhost:{port} Link1 secret")
            c1 = p1.wait(lambda e: e.get("event") == "connected", what="Link1 connected")
            print(f"ok: both players connected (slots {c1['slot']} and {c2['slot']})")

            for c in (c1, c2):
                data = c["slot_data"]
                assert data["slot_data_version"] == 1, data.keys()
                assert len(data["locations"]) == c["missing"], (len(data["locations"]), c["missing"])
                assert c["checked"] == 0
            print(f"ok: slot data matches the server ({len(c1['slot_data']['locations'])} locations)")

            # A Link1 location holding a Link2 item
            sent = next(loc for loc in c1["slot_data"]["locations"] if loc[2] == "Link2")
            loc_name, loc_id, _, item_name, _, _ = sent
            m1 = p1.mark()
            p1.send(f"check {loc_id}")
            got = p2.wait(lambda e: e.get("event") == "items" and any(i[1] == loc_id for i in e["items"]),
                          what=f"Link2 receiving {item_name}")
            item = next(i for i in got["items"] if i[1] == loc_id)
            assert item[2] == c1["slot"], item
            p1.wait(lambda e: e.get("event") == "checked" and e["count"] == 1, what="Link1 RoomUpdate", since=m1)
            msg = p1.wait(lambda e: e.get("event") == "message" and e["type"] == "ItemSend",
                          what="ItemSend message", since=m1)
            print(f"ok: Link1 checked '{loc_name}', Link2 received '{item_name}'; message: {msg['text']}")
            assert item_name in msg["text"] and loc_name in msg["text"], msg["text"]
            print("ok: item and location names resolved from the data package")

            # Link2 finds Link1's item; Link1 gets it live
            back = next(loc for loc in c2["slot_data"]["locations"] if loc[2] == "Link1")
            p2.send(f"check {back[1]}")
            got1 = p1.wait(lambda e: e.get("event") == "items" and any(i[1] == back[1] for i in e["items"]),
                           what="Link1 receiving")
            print(f"ok: Link1 received '{back[3]}' from Link2 ({len(got1['items'])} item(s) total)")

            # Checking the same location again is harmless; already-checked list survives reconnect
            p1.send(f"check {loc_id}")
            p1.send("disconnect")
            p1.wait(lambda e: e.get("event") == "state" and e["state"] == "Not connected", what="disconnect")
            m1 = p1.mark()
            p1.send(f"connect 127.0.0.1:{port} Link1 secret")
            again = p1.wait(lambda e: e.get("event") == "connected", what="Link1 reconnected", since=m1)
            assert again["checked"] == 1, again["checked"]
            replay = p1.wait(lambda e: e.get("event") == "items", what="items replayed after reconnect", since=m1)
            assert [i[1] for i in replay["items"]] == [back[1]], replay["items"]
            print("ok: reconnect replays received items from index 0 and keeps checked locations")

            # Batch of checks, including ones that belong to Link1 itself
            batch = [loc[1] for loc in c1["slot_data"]["locations"][:25]]
            m1 = p1.mark()
            p1.send("check " + " ".join(map(str, batch)))
            p1.wait(lambda e: e.get("event") == "checked" and e["count"] >= len(set(batch) | {loc_id}),
                    what="batch of checks acknowledged")
            own = [loc for loc in c1["slot_data"]["locations"][:25] if loc[5]]
            p1.drain(1.0)
            last_items = [e for e in p1.log[m1:] if e.get("event") == "items"]
            own_ids = {loc[1] for loc in own}
            assert not any(i[1] in own_ids for e in last_items for i in e["items"]), \
                "own-world items must not be sent back (items_handling 0b101)"
            print(f"ok: 25 checks acknowledged; {len(own)} own-world item(s) not echoed back")

            # Names
            p1.send(f"names {item[0]} {c2['slot']}")
            names = p1.wait(lambda e: e.get("event") == "names", what="names")
            assert names["item"] == item_name and names["player"] == "Link2", names
            print("ok: item and player names")

            # DeathLink
            p1.send("deathlink on")
            p2.send("deathlink on")
            time.sleep(0.5)
            m1 = p1.mark()
            p1.send("die fell into a pit")
            dl = p2.wait(lambda e: e.get("event") == "deathlink", what="DeathLink on Link2")
            assert dl["source"] == "Link1" and dl["cause"] == "fell into a pit", dl
            p1.drain(1.0)
            assert not any(e.get("event") == "deathlink" for e in p1.log[m1:]), "own DeathLink came back"
            p2.send("deathlink off")
            time.sleep(0.5)
            m2 = p2.mark()
            p1.send("die again")
            p2.drain(1.5)
            assert not any(e.get("event") == "deathlink" for e in p2.log[m2:]), \
                "DeathLink received after turning it off"
            print("ok: DeathLink sent, received, not echoed, and can be turned off")

            # Chat and goal
            p1.send("say hello from the client test")
            p2.wait(lambda e: e.get("event") == "message" and "hello from the client test" in e["text"],
                    what="chat message")
            p1.send("goal")
            p2.wait(lambda e: e.get("event") == "message" and e["type"] == "Goal", what="goal message")
            print("ok: chat and goal")

            # A new connection never carries over the previous slot's items
            p2.send(f"connect-once localhost:{port} Nobody secret")
            p2.send("count")
            count = p2.wait(lambda e: e.get("event") == "count", what="item count after a new connection")
            assert count["items"] == 0 and count["checked"] == 0, count
            print("ok: a new connection starts without the previous slot's items")

            # Connection loss and automatic reconnection
            server.terminate()
            server.wait(timeout=10)
            p1.wait(lambda e: e.get("event") == "state" and e["state"] == "Reconnecting", what="reconnecting")
            server = start_server(args, multidata, port)
            p1.wait(lambda e: e.get("event") == "connected", timeout=60, what="automatic reconnection")
            print("ok: automatic reconnection after the server restarted")
        finally:
            for d in drivers:
                d.close()
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
    print("ALL CLIENT TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
