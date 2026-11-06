#!/usr/bin/env python3
"""The regression suite's speed twin: same cases, same verdicts, plus a
real-time multiplier per line — verdicts gate the exit code, never the speed.

Thin wrapper over tools/regression.py (--perf): every case additionally
reports the binary's own "Time in modem_demodulate" and the real-time
multiplier xRT = audio duration / modem time, so the zero-copy and
vectorization entries can prove their numbers against this baseline.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regression

if __name__ == "__main__":
    sys.exit(regression.main(["--perf"] + sys.argv[1:]))
