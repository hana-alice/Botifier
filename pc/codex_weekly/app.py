"""Console helper: official account metadata only, no model requests."""

import argparse
import json
import sys
import time

from .client import AppServerClient
from .quota import QuotaError, SnapshotStore, safe_summary, select_weekly
from .transport import SerialLink


def report(frame):
    print(json.dumps(safe_summary(frame), separators=(',', ':'), allow_nan=False), flush=True)


def query(client, store):
    try:
        frame = store.publish(client.fetch())
    except (QuotaError, OSError, ValueError, TypeError, OverflowError) as error:
        frame = store.publish(None)
        code = error.code if isinstance(error, QuotaError) else 'read_failed'
        rpc_code = error.rpc_code if isinstance(error, QuotaError) else None
        suffix = f' code={rpc_code}' if rpc_code is not None else ''
        print('Codex weekly unavailable: ' + code + suffix, file=sys.stderr, flush=True)
    report(frame)
    return frame.quota is not None


def simulation():
    now = [1_790_000_000, 0.0]
    store = SnapshotStore(lambda: now[0], lambda: now[1])
    for position, used in (('primary', 24), ('secondary', 100), ('primary', 0)):
        result = {'rateLimitsByLimitId': {'codex': {position: {
            'usedPercent': used, 'windowDurationMins': 10080, 'resetsAt': now[0] + 604800}}}}
        report(store.publish(select_weekly(result, now[0], now[1])))
    now[0] += 901
    now[1] += 901
    report(store.current())
    print('Offline simulation complete; no account query or serial access.', file=sys.stderr)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--once', action='store_true', help='Read and print quota once; never open serial')
    parser.add_argument('--simulate', action='store_true', help='Offline simulation; no API or serial access')
    parser.add_argument('--port', help='USB port; default requires unique VID303A/PID1001')
    parser.add_argument('--poll-seconds', type=int, default=300, help='Account refresh interval, 60..3600 (default 300)')
    args = parser.parse_args(argv)
    if not 60 <= args.poll_seconds <= 3600:
        parser.error('--poll-seconds must be between 60 and 3600')
    if args.port is not None and (not args.port.strip() or len(args.port) > 128 or any(ord(c) < 32 for c in args.port)):
        parser.error('--port must be a nonempty serial port name')
    link = None
    try:
        if args.simulate:
            return simulation()
        store, client = SnapshotStore(), AppServerClient()
        if args.once:
            return 0 if query(client, store) else 1
        link = SerialLink(store, args.port)
        link.start()
        while True:
            started = time.monotonic()
            query(client, store)
            time.sleep(max(0, args.poll_seconds - (time.monotonic() - started)))
    except KeyboardInterrupt:
        print('Codex weekly stopped.', file=sys.stderr, flush=True)
        return 0
    except (OSError, ValueError, ImportError, QuotaError):
        print('Codex weekly unavailable: helper_failed', file=sys.stderr, flush=True)
        return 1
    finally:
        if link is not None:
            link.stop()
