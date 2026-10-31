#!/usr/bin/env python3
"""The texts of the PB Store interface are keys of bridge/strings/strings.tsv: qsTr("home.refresh") in the QML, tr("store.never") in the bridge.

    scripts/extract-strings.py            print the keys that the sources use
    scripts/extract-strings.py --check    compare with the table: a key used but not in the table, a key in the table that nothing uses,
                                          a text whose placeholders (%1, %2 ...) differ between uses, a qsTr()/tr() that is not a plain key

The check is what `ctest -R check_strings` runs (task T075). Only plain string literals are found; a call that builds its key by joining is reported.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "bridge" / "strings" / "strings.tsv"
KEY = re.compile(r"^[a-z][a-z0-9]*(\.[a-z0-9_]+)+$")
QML_CALL = re.compile(r'qsTr\(\s*("(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|[^"\')\s][^,)]*)')
CPP_CALL = re.compile(r'(?<![A-Za-z_])tr\(\s*("(?:[^"\\]|\\.)*"|[^"\s)][^,)]*)')


def sources():
    used, bad = {}, []
    files = [(p, QML_CALL) for p in sorted((ROOT / "qml").rglob("*.qml"))]
    files += [(p, CPP_CALL) for p in sorted((ROOT / "bridge" / "src").glob("*.cpp"))]
    for path, rx in files:
        rel = path.relative_to(ROOT).as_posix()
        for m in rx.finditer(path.read_text(encoding="utf-8")):
            arg = m.group(1)
            if len(arg) >= 2 and arg[0] in "\"'" and arg[-1] == arg[0]:
                key = arg[1:-1]
                if KEY.match(key):
                    used.setdefault(key, rel)
                else:
                    bad.append(f"{rel}: not a key: {arg[:60]}")
            else:
                bad.append(f"{rel}: not a plain string: {arg[:60]}")
    return used, bad


def table():
    entries, problems = {}, []
    for n, line in enumerate(TABLE.read_text(encoding="utf-8").split("\n"), 1):
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 2:
            problems.append(f"{TABLE.name}:{n}: a line needs exactly one TAB")
            continue
        key, text = parts
        if not KEY.match(key):
            problems.append(f"{TABLE.name}:{n}: not a key: {key}")
        if key in entries:
            problems.append(f"{TABLE.name}:{n}: key twice: {key}")
        if not text.strip():
            problems.append(f"{TABLE.name}:{n}: empty text for {key}")
        entries[key] = text
    return entries, problems


def main():
    used, bad = sources()
    if "--check" not in sys.argv:
        for key in sorted(used):
            print(key)
        return 0
    entries, problems = table()
    problems += bad
    for key in sorted(set(used) - set(entries)):
        problems.append(f"missing in the table: {key} (used in {used[key]})")
    for key in sorted(set(entries) - set(used)):
        problems.append(f"in the table, used nowhere: {key}")
    for p in problems:
        print(p, file=sys.stderr)
    if problems:
        return 1
    print(f"{len(entries)} keys, all used, none missing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
