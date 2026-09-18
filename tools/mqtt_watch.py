#!/usr/bin/env python3
"""Subscribe to an MQTT broker for a bounded window, print what arrives, exit.

`mosquitto_sub` never exits, so a caller that waits for it hangs. This listens for a
fixed number of seconds (or until --expect / --count is met), prints messages,
and returns a status code.

Silence is a failure: exit 0 only if the subscription succeeded and the
condition was met (or, with no condition, at least one message arrived).

Neither a broker address nor credentials are defaulted.

Examples
--------
    python tools/mqtt_watch.py --host 192.0.2.20 \\
        --topic "data/devices/utility_meter/#" --seconds 20
    python tools/mqtt_watch.py --host 192.0.2.20 \\
        --topic "data/devices/utility_meter" --expect online --seconds 10
    python tools/mqtt_watch.py --host 192.0.2.20 \\
        --topic "data/devices/utility_meter" --topic "data/devices/utility_meter/#" \\
        --steal-client-id P1AABBCC --seconds 30

Requires paho-mqtt. Install into the PlatformIO interpreter:

    python -m pip install -r tools/requirements.txt
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit(
        "paho-mqtt is not available to this Python interpreter.\n"
        "Install it with the PlatformIO interpreter:\n"
        "  python -m pip install -r tools/requirements.txt"
    )


def make_client(client_id: str | None = None) -> "mqtt.Client":
    kwargs: dict = {}
    if hasattr(mqtt, "CallbackAPIVersion"):
        kwargs["callback_api_version"] = mqtt.CallbackAPIVersion.VERSION1
    if client_id:
        kwargs["client_id"] = client_id
    return mqtt.Client(**kwargs)


def decode_payload(raw: bytes) -> str:
    return raw.decode("utf-8", errors="replace")


def note(t0: float, text: str, as_json: bool) -> None:
    elapsed = time.monotonic() - t0
    if as_json:
        print(json.dumps({"event": text, "t": round(elapsed, 3)}), flush=True)
    else:
        print(f"# t={elapsed:.1f}s {text}", flush=True)


def steal_session(host: str, port: int, client_id: str, user: str | None,
                  password: str | None, t0: float, as_json: bool) -> bool:
    """Connect once as client_id (kicking the existing session), then leave."""
    thief = make_client(client_id)
    rc_holder = {"rc": None}

    def on_connect(client, userdata, flags, rc, properties=None):
        rc_holder["rc"] = rc

    thief.on_connect = on_connect
    if user is not None:
        thief.username_pw_set(user, password)

    note(t0, f"steal connecting as {client_id}", as_json)
    try:
        thief.connect(host, port, keepalive=30)
    except Exception as exc:
        note(t0, f"steal connect failed: {exc}", as_json)
        return False

    thief.loop_start()
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline and rc_holder["rc"] is None:
        time.sleep(0.05)
    ok = rc_holder["rc"] == 0
    note(t0, f"steal connected rc={rc_holder['rc']}", as_json)
    time.sleep(0.4)
    thief.loop_stop()
    thief.disconnect()
    note(t0, "steal disconnected", as_json)
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Subscribe to MQTT for a bounded window and print messages.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--host", required=True, help="Broker hostname or IP. No default.")
    ap.add_argument("--port", type=int, default=1883, help="Broker port (default 1883).")
    ap.add_argument("--user", default=None, help="Optional username.")
    ap.add_argument("--pass", dest="password", default=None, help="Optional password.")
    ap.add_argument("--topic", action="append", default=[], metavar="T",
                    help="Topic to subscribe; repeatable.")
    ap.add_argument("--seconds", type=float, default=15.0,
                    help="How long to listen (default 15).")
    ap.add_argument("--expect", metavar="TEXT",
                    help="Exit 0 as soon as a payload contains this.")
    ap.add_argument("--count", type=int, metavar="N",
                    help="Exit 0 once N messages have arrived.")
    ap.add_argument("--retained-only", action="store_true",
                    help="Print only messages flagged retained.")
    ap.add_argument("--json", action="store_true",
                    help="Machine-readable JSON lines.")
    ap.add_argument("--steal-client-id", metavar="ID",
                    help="After the first live message, connect once with this "
                         "client id to kick the existing session, then disconnect.")
    args = ap.parse_args()

    if not args.topic:
        ap.error("at least one --topic is required")
    if args.count is not None and args.count < 1:
        ap.error("--count must be >= 1")

    seen: list[tuple[str, str, bool]] = []
    expect_hit = False
    count_hit = False
    connected = False
    connect_rc = None
    stop = False
    steal_at: float | None = None
    stole = False
    steal_ok = False
    saw_offline_after = False
    saw_online_after = False
    saw_reading_after = False
    online_after_at: float | None = None

    def on_connect(client, userdata, flags, rc, properties=None):
        nonlocal connected, connect_rc
        connect_rc = rc
        connected = rc == 0
        if not connected:
            return
        for t in args.topic:
            client.subscribe(t)

    def on_message(client, userdata, msg):
        nonlocal expect_hit, count_hit, stop
        nonlocal saw_offline_after, saw_online_after, saw_reading_after, online_after_at
        retained = bool(msg.retain)
        if args.retained_only and not retained:
            return
        payload = decode_payload(msg.payload)
        seen.append((msg.topic, payload, retained))
        if args.json:
            print(json.dumps({
                "topic": msg.topic,
                "payload": payload,
                "retained": retained,
            }, ensure_ascii=False), flush=True)
        else:
            mark = " [retained]" if retained else ""
            print(f"{msg.topic}{mark} {payload}", flush=True)
        if steal_at is not None and not retained:
            pl = payload.strip()
            if pl == "offline":
                saw_offline_after = True
            elif pl == "online":
                if not saw_online_after:
                    online_after_at = time.monotonic()
                saw_online_after = True
            elif pl not in ("online", "offline"):
                saw_reading_after = True
        if args.steal_client_id:
            return
        if args.expect is not None and args.expect in payload:
            expect_hit = True
            stop = True
        if args.count is not None and len(seen) >= args.count:
            count_hit = True
            stop = True

    watcher_id = f"mqtt-watch-{os.getpid()}"
    client = make_client(watcher_id)
    client.on_connect = on_connect
    client.on_message = on_message
    if args.user is not None:
        client.username_pw_set(args.user, args.password)

    t0 = time.monotonic()
    try:
        client.connect(args.host, args.port, keepalive=30)
    except Exception as exc:
        print(f"connect failed: {exc}", file=sys.stderr)
        return 1

    client.loop_start()
    deadline = time.monotonic() + args.seconds
    steal_after_live = args.steal_client_id is not None
    try:
        while time.monotonic() < deadline and not stop:
            if connect_rc is not None and not connected:
                break
            if steal_after_live and not stole and connected:
                live = any(not r for _, _, r in seen)
                waited = time.monotonic() - t0
                if live or waited >= 3.0:
                    steal_at = time.monotonic()
                    steal_ok = steal_session(
                        args.host, args.port, args.steal_client_id,
                        args.user, args.password, t0, args.json,
                    )
                    stole = True
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()

    elapsed = time.monotonic() - t0
    topics = {t for t, _, _ in seen}

    if args.steal_client_id:
        extra = (
            f"steal done ok={steal_ok} offline={saw_offline_after} "
            f"online={saw_online_after} readings={saw_reading_after}"
        )
        if steal_at is not None and online_after_at is not None:
            extra += f" reconnect={online_after_at - steal_at:.1f}s"
        note(t0, extra, args.json)

    if args.json:
        summary = {
            "summary": True,
            "count": len(seen),
            "topics": len(topics),
            "elapsed": round(elapsed, 3),
            "connected": connected,
        }
        if args.steal_client_id:
            summary.update({
                "stole": stole,
                "steal_ok": steal_ok,
                "offline_after_steal": saw_offline_after,
                "online_after_steal": saw_online_after,
                "readings_after_steal": saw_reading_after,
            })
        print(json.dumps(summary), flush=True)
    else:
        print(
            f"# {len(seen)} messages, {len(topics)} topics, {elapsed:.1f}s",
            flush=True,
        )

    if not connected:
        print(f"connect failed: rc={connect_rc}", file=sys.stderr)
        return 1
    if args.steal_client_id:
        if not steal_ok:
            return 1
        # Reconnect half is required. Missing last will is a broker finding.
        return 0 if saw_online_after else 1
    if args.expect is not None:
        return 0 if expect_hit else 1
    if args.count is not None:
        return 0 if count_hit else 1
    return 0 if seen else 1


if __name__ == "__main__":
    raise SystemExit(main())
