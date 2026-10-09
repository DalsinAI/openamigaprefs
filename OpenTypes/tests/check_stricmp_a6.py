#!/usr/bin/env python3
"""check_stricmp_a6.py [STOVE]: builds OpenTypes and checks, in its machine code, that every call of
utility.library's Stricmp (jsr a6@(-162)) is made with A6 holding UtilityBase.

OpenTypes 0.2 compared tool names as Stricmp(base(a), base(b)); GCC 6.5 then called Stricmp with A6 still
holding dos.library, so the call went to a dos.library vector and returned 0 ("equal") for every pair, and
OpenUp 0.6.14 to 0.7.0 changed the default tool of about 40 icons of AmigaOS 3.2.3 to OpenView (9 Oct 2026).
The names are compared by same_program() now, which calls no library.

The base of a call is the operand of the last `moveal X,a6` before it; all calls must load the same X (the
UtilityBase variable). Skipped (exit 0) when the stove isn't there. Exit 1 on a call with another A6.
Needs no Amiga: the stove's compiler and objdump only."""
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
STOVE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get("STOVE", Path.home() / "AmigaChrome/stoves/os32"))
OBJDUMP = STOVE / "prefix/bin/m68k-amigaos-objdump"


def a6_sources(disassembly: str) -> list[tuple[str, str]]:
    """(address, last A6 source) for every jsr a6@(-162)."""
    last, found = None, []
    for line in disassembly.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s", line)
        if not m:
            continue
        load = re.search(r"moveal (\S+?)(?: <[^>]*>)?,a6\s*$", line)
        if load:
            last = load.group(1)
        elif re.search(r"\b(?:lea|movel|moveal|exg)\b.*\ba6\b", line) and re.search(r",a6\s*$", line.strip()):
            last = "?"
        if "jsr a6@(-162)" in line:
            found.append((m.group(1), last))
    return found


def main() -> int:
    if not OBJDUMP.is_file():
        print(f"skipped: no stove at {STOVE}")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["sh", str(HERE / "build.sh"), tmp], check=True, stdout=subprocess.DEVNULL, env=dict(os.environ, STOVE=str(STOVE)))
        text = subprocess.run([str(OBJDUMP), "-d", str(Path(tmp) / "OpenTypes")], check=True, capture_output=True, text=True).stdout
    calls = a6_sources(text)
    if not calls:
        print("FAIL: no call of Stricmp found; the check needs updating")
        return 1
    base, _ = Counter(src for _, src in calls).most_common(1)[0]
    bad = [(a, s) for a, s in calls if s != base]
    for a, s in bad:
        print(f"FAIL: Stricmp at {a} is called with A6 from {s}, not UtilityBase ({base})")
    print(f"{len(calls)} calls of Stricmp, {len(bad)} with the wrong A6")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
