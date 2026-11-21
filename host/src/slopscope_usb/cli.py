"""Small command-line entry points for SlopScope USB."""
import argparse
import json
import logging


def bridge_options(parser):
    parser.add_argument("--serial", help="Scope USB serial; choose explicitly when several are connected")
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--ui-dir", help="Use a local viewer static directory instead of downloading it")
    parser.add_argument("--viewer-url", help="Published viewer URL; defaults to SlopScope's GitHub Pages site")
    parser.add_argument("--no-usb-recovery", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--capture-usb-errors", help=argparse.SUPPRESS)


def run_bridge(args, parser):
    from .bridge import serve
    logging.basicConfig(format="%(asctime)s %(levelname)s %(message)s")
    try:
        serve(port=args.port, open_browser=not args.no_browser, serial_number=args.serial,
              ui_dir=args.ui_dir, viewer_url=args.viewer_url,
              recover_usb=not args.no_usb_recovery, capture_directory=args.capture_usb_errors)
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f"{error}\n")


def bridge_main():
    parser = argparse.ArgumentParser(description="Open the SlopScope USB viewer through Python")
    bridge_options(parser)
    run_bridge(parser.parse_args(), parser)


def main():
    parser = argparse.ArgumentParser(description="Control SlopScope firmware over USB")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="List connected SlopScope units")
    info = commands.add_parser("info", help="Read scope settings")
    info.add_argument("--serial")
    bridge_options(commands.add_parser("bridge", help="Start the local viewer bridge"))
    args = parser.parse_args()
    if args.command == "bridge":
        run_bridge(args, parser)
        return
    from . import SlopScope, list_scopes
    try:
        if args.command == "list":
            result = list_scopes()
        else:
            with SlopScope(serial_number=args.serial) as scope:
                result = scope.settings()
        print(json.dumps(result, indent=2))
    except RuntimeError as error:
        parser.exit(1, f"{error}\n")
