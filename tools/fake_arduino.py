#!/usr/bin/env python3
"""A board that speaks the SimuPy bridge protocol, without a board.

Creates a pseudo-terminal, prints its path, and behaves like a microcontroller
running `firmware/simupy_bridge`. Point a model's Arduino blocks at that path
and everything works exactly as it would over USB — which makes it useful for
two things:

  * building and debugging a model with no hardware to hand,
  * testing the host side, which is what the test suite uses it for.

    $ python3 tools/fake_arduino.py
    /dev/pts/7
    $ # set the Arduino blocks' Port to that, and run

By default every analog input reads a slow sine, so a plot shows something
recognisable. `--echo` instead makes analog input n report whatever was last
written to PWM pin n, which is what you want when testing a loop end to end.
"""

import argparse
import math
import os
import pty
import selectors
import sys
import termios
import time
import tty

VERSION = 1

class FakeBoard:
    """The firmware's behaviour, in Python."""

    def __init__(self, board="fake", adc_bits=10, pwm_bits=8, mode="sine"):
        self.board = board
        self.adc_bits = adc_bits
        self.pwm_bits = pwm_bits
        self.adc_max = (1 << adc_bits) - 1
        self.mode = mode

        self.stream = []
        self.generation = 0
        self.rate_hz = 200
        self.digital = {}
        self.pwm = {}
        self.modes = {}
        self.started = time.monotonic()

    def read_analog(self, pin):
        if self.mode == "echo":
            return int(round(self.pwm.get(pin, 0.0) * self.adc_max))
        t = time.monotonic() - self.started
        phase = 0.25 * pin
        return int(round((0.5 + 0.45 * math.sin(2 * math.pi * (0.2 * t + phase)))
                         * self.adc_max))

    def read_digital(self, pin):
        if self.mode == "echo":
            return self.digital.get(pin, 0)
        return 1 if int(time.monotonic() - self.started) % 2 else 0

    def identify(self):
        return (f"#simupy {VERSION} {self.board} {self.adc_bits} "
                f"{self.pwm_bits} 8 20")

    @staticmethod
    def parse_pin(token):
        """'A3' -> (3, True); 'D7' or '7' -> (7, False)."""
        if not token:
            return None
        head, digits = token[0], token[1:]
        if head in "Aa":
            analog = True
        elif head in "Dd":
            analog = False
        else:
            analog, digits = False, token
        if not digits.isdigit():
            return None
        return int(digits), analog

    def command(self, line):
        """Returns a reply line, or None."""
        parts = line.split()
        if not parts:
            return None
        verb, args = parts[0], parts[1:]

        if verb == "?":
            return self.identify()

        if verb in "Cc":
            pins = [self.parse_pin(a) for a in args]
            if any(p is None for p in pins):
                return "!bad pin"
            if len(pins) > 16:
                return "!too many pins"
            self.stream = pins
            self.generation += 1
            return f"#C {self.generation}"

        if verb in "Rr":
            if not args:
                return "!R needs a rate"
            self.rate_hz = max(1, min(2000, int(args[0])))
            return None

        if verb in "Mm":
            if len(args) < 2:
                return "!M needs a pin and a mode"
            pin = self.parse_pin(args[0])
            if pin is None:
                return "!bad pin"
            self.modes[pin[0]] = args[1][0].upper()
            return None

        if verb in "Ww":
            if len(args) < 2:
                return "!W needs a pin and a level"
            pin = self.parse_pin(args[0])
            if pin is None:
                return "!bad pin"
            self.digital[pin[0]] = 1 if int(args[1]) else 0
            return None

        if verb in "Pp":
            if len(args) < 2:
                return "!P needs a pin and a duty"
            pin = self.parse_pin(args[0])
            if pin is None:
                return "!bad pin"
            self.pwm[pin[0]] = max(0.0, min(1.0, float(args[1])))
            return None

        return "!unknown command"

    def sample(self):
        micros = int((time.monotonic() - self.started) * 1e6)
        values = [self.read_analog(pin) if analog else self.read_digital(pin)
                  for pin, analog in self.stream]
        return " ".join(str(v) for v in
                        [self.generation, micros, *values])

def serve(board, verbose=False, seconds=0.0):
    """Runs `board` on a fresh pty and prints the path to stdout."""
    primary, secondary = pty.openpty()
    path = os.ttyname(secondary)

    tty.setraw(secondary)
    tty.setraw(primary)

    print(path, flush=True)

    selector = selectors.DefaultSelector()
    selector.register(primary, selectors.EVENT_READ)

    def send(text):
        os.write(primary, (text + "\n").encode("ascii"))
        if verbose:
            print(f"<- {text}", file=sys.stderr)

    send(board.identify())

    pending = b""
    next_due = time.monotonic()
    deadline = (time.monotonic() + seconds) if seconds > 0 else None

    try:
        while True:
            if deadline is not None and time.monotonic() > deadline:
                return
            period = 1.0 / board.rate_hz
            timeout = max(0.0, next_due - time.monotonic())
            for _ in selector.select(timeout=min(timeout, 0.05)):
                try:
                    chunk = os.read(primary, 4096)
                except OSError:
                    return
                if not chunk:
                    return
                pending += chunk
                while b"\n" in pending:
                    raw, pending = pending.split(b"\n", 1)
                    line = raw.decode("ascii", "replace").strip()
                    if verbose and line:
                        print(f"-> {line}", file=sys.stderr)
                    reply = board.command(line)
                    if reply:
                        send(reply)

            now = time.monotonic()
            if board.stream and now >= next_due:
                next_due = max(now, next_due + period)
                send(board.sample())
            elif not board.stream:
                next_due = now
    except KeyboardInterrupt:
        pass
    finally:
        os.close(primary)
        os.close(secondary)

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("sine", "echo"), default="sine",
                        help="what the inputs read: a slow sine, or whatever "
                             "was last written to the matching output")
    parser.add_argument("--board", default="fake", help="name to report")
    parser.add_argument("--adc-bits", type=int, default=10)
    parser.add_argument("--pwm-bits", type=int, default=8)
    parser.add_argument("--seconds", type=float, default=0.0,
                        help="exit after this long. A safety net for scripts "
                             "and tests, so a leaked board does not sit on a "
                             "pty forever. 0 runs until interrupted.")
    parser.add_argument("--verbose", action="store_true",
                        help="trace the conversation on stderr")
    args = parser.parse_args()

    serve(FakeBoard(args.board, args.adc_bits, args.pwm_bits, args.mode),
          args.verbose, args.seconds)

if __name__ == "__main__":
    main()
