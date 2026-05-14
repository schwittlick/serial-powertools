#!/usr/bin/env python3
"""
fake_plotter.py — minimal HPGL responder for integration tests.

Opens a serial / pty device, reads bytes, logs each command, and replies to
HPGL/ESC queries the way a real plotter would. Used together with `socat` to
stand up a virtual plotter the C++ binary can talk to.

Layout (slave end of the pty pair → fake_plotter, master end → C++ binary):

    socat -d -d pty,raw,echo=0,link=/tmp/fake_a pty,raw,echo=0,link=/tmp/fake_b
    python3 fake_plotter.py --tty /tmp/fake_a --model 7550A --log /tmp/p.log &
    integration_plotter /tmp/fake_b

Responses (terminated with CR = 0x0D):
    ESC.A         -> "<model>,1.0"
    ESC.B         -> "1024"
    ESC.L         -> "0"
    ESC.O         -> "0,0,0"
    ESC.K / .R    -> (no reply)
    ESC.T<...>:   -> (no reply, memory alloc)
    ESC.@<...>:   -> (no reply, logical buffer)
    OI;           -> "<model>"
    OH;           -> "0,0,16640,10160"
    OA;           -> "0,0,0"
    OE;           -> "0"

Anything else is just logged.
"""

import argparse
import os
import sys
import time

ESC = 0x1B
CR = b"\r"
LB_TERM = 0x03

# Fixed-form ESC sequences: ESC . <letter> -> total 3 bytes, no payload.
ESC_FIXED = set("ABKLRO")
# Variable-length ESC sequences: ESC . <letter> <payload> ":"
ESC_VAR = set("T@")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tty", required=True, help="device path (e.g. /tmp/fake_a)")
    ap.add_argument("--model", default="7470A",
                    help="model identification string (default: 7470A)")
    ap.add_argument("--log", required=True, help="path to write event log")
    args = ap.parse_args()

    # O_NOCTTY so we don't become a controlling tty.
    fd = os.open(args.tty, os.O_RDWR | os.O_NOCTTY)
    log = open(args.log, "w", buffering=1)
    log.write(f"# fake_plotter model={args.model} tty={args.tty} pid={os.getpid()}\n")

    def write_reply(s: str):
        os.write(fd, s.encode("latin-1") + CR)
        log.write(f"REPLY: {s}\\r\n")

    def read_byte() -> int:
        while True:
            b = os.read(fd, 1)
            if b:
                return b[0]
            # No data — short sleep to keep CPU low.
            time.sleep(0.001)

    buf = bytearray()

    def flush_command(end_label: str):
        if not buf:
            return
        cmd = buf.decode("latin-1", errors="replace")
        log.write(f"CMD ({end_label}): {cmd}\n")
        # The first two chars are the HPGL mnemonic.
        if cmd.startswith("OI"):
            write_reply(args.model)
        elif cmd.startswith("OH"):
            write_reply("0,0,16640,10160")
        elif cmd.startswith("OA"):
            write_reply("0,0,0")
        elif cmd.startswith("OE"):
            write_reply("0")
        buf.clear()

    try:
        while True:
            x = read_byte()
            if x == ESC:
                b2 = read_byte()
                if b2 != ord("."):
                    log.write(f"UNKNOWN ESC byte: {b2:#x}\n")
                    continue
                letter = chr(read_byte())
                if letter in ESC_FIXED:
                    log.write(f"ESC.{letter}\n")
                    if letter == "A":
                        write_reply(f"{args.model},1.0")
                    elif letter == "B":
                        write_reply("1024")
                    elif letter == "L":
                        write_reply("0")
                    elif letter == "O":
                        write_reply("0,0,0")
                    # K, R: no reply
                elif letter in ESC_VAR:
                    payload = bytearray()
                    while True:
                        y = read_byte()
                        if y == ord(":"):
                            break
                        payload.append(y)
                    log.write(
                        f"ESC.{letter}{payload.decode('latin-1', errors='replace')}:\n"
                    )
                    # ESC.T / ESC.@ do not produce a reply directly; the
                    # HpglPlotter code follows them with ESC.L which does.
                else:
                    log.write(f"UNKNOWN ESC.{letter}\n")
                continue

            if x == ord(";"):
                flush_command(";")
            elif x == LB_TERM:
                flush_command("LB")
            elif x in (ord("\r"), ord("\n")):
                flush_command("NL")
            else:
                buf.append(x)
    except KeyboardInterrupt:
        log.write("# interrupted\n")
    except Exception as e:
        log.write(f"# error: {e!r}\n")
        sys.exit(1)
    finally:
        flush_command("EOF")
        log.close()
        os.close(fd)


if __name__ == "__main__":
    main()
