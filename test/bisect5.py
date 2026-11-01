#!/usr/bin/env python3
"""Find the statement that makes a roast5 file fail to parse.

usage: test/bisect5.py FILE [FILE...]      (paths relative to test/roast5 or absolute)

p2's parse error text ("near token X before text {") names the start of the
enclosing block, not the failing construct.  This cuts the file into top-level
chunks, finds the first chunk that breaks the parse (accumulated, with `-c`:
parse only, nothing runs), then descends into that chunk: each inner statement
is parsed alone inside the chunk's header line and closing brace, and any that
still fail are reported (recursively, for nested blocks).
"""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
P2 = os.path.join(ROOT, "bin", "p2")
CWD = os.path.join(ROOT, "test", "roast5")


def parse_fails(text):
    with tempfile.NamedTemporaryFile("w", suffix=".pl", delete=False, encoding="latin-1") as f:
        f.write(text)
        path = f.name
    try:
        r = subprocess.run([P2, "-c", path], cwd=CWD, capture_output=True, timeout=10,
                           text=True, errors="replace")
    except subprocess.TimeoutExpired:
        return "timeout"
    finally:
        os.unlink(path)
    err = r.stderr
    if r.returncode != 0 and ("Syntax error" in err or "parser error" in err):
        return err.strip().splitlines()[0]
    return None


def depth_delta(line):
    """Net brace/paren/bracket depth change of a line, ignoring quotes, comments, escapes."""
    d, q, i = 0, None, 0
    while i < len(line):
        ch = line[i]
        if ch == "\\":
            i += 2
            continue
        if q:
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
        elif ch == "#":
            break
        elif ch in "{([":
            d += 1
        elif ch in "})]":
            d -= 1
        i += 1
    return d


def chunks(lines, split_on_blank):
    """Split at depth 0.  Top level: blank/comment lines end a chunk; inside a block: a line ending in ; or }."""
    out, cur, depth = [], [], 0
    for line in lines:
        cur.append(line)
        depth += depth_delta(line)
        s = line.strip()
        if depth <= 0:
            end = (s == "" or s.startswith("#")) if split_on_blank else s.endswith((";", "}", "};"))
            if end:
                out.append(cur)
                cur = []
    if cur:
        out.append(cur)
    return [c for c in out if any(l.strip() and not l.strip().startswith("#") for l in c)]


def descend(header, body_lines, footer, indent, found):
    inner = chunks(body_lines, False)
    hit = False
    for c in inner:
        text = "".join(header) + "".join(c) + "".join(footer)
        err = parse_fails(text)
        if not err:
            continue
        hit = True
        stripped = [l for l in c]
        first = stripped[0].strip()
        sub_header, sub_body, sub_footer = None, None, None
        # a nested block: header = first line ending in "{", footer = the last line
        if len(stripped) > 2 and depth_delta(stripped[0]) > 0 and stripped[0].rstrip().endswith("{"):
            sub_header = header + [stripped[0]]
            sub_body = stripped[1:-1]
            sub_footer = [stripped[-1]] + footer
            if descend(sub_header, sub_body, sub_footer, indent + "  ", found):
                continue
        found.append((indent, first[:100], err))
    return hit


def bisect(path):
    full = path if os.path.isabs(path) else os.path.join(CWD, path)
    lines = open(full, encoding="latin-1").readlines()
    segs = chunks(lines, True)
    acc = ""
    for idx, seg in enumerate(segs):
        nxt = acc + "".join(seg)
        err = parse_fails(nxt)
        if not err:
            acc = nxt
            continue
        print(f"{path}: top-level chunk {idx} breaks the parse ({err[:90]})")
        found = []
        while seg and (seg[-1].strip() == "" or seg[-1].strip().startswith("#")):
            seg = seg[:-1]
        first = seg[0]
        if len(seg) > 2 and first.rstrip().endswith("{") and depth_delta(first) > 0:
            descend([first], seg[1:-1], [seg[-1]], "  ", found)
        if not found:
            print("  (not narrowed below the chunk)  " + "".join(seg).strip()[:200].replace("\n", " | "))
        for indent, stmt, e in found:
            print(f"{indent}-> {stmt}")
        return
    print(f"{path}: parses ({len(segs)} chunks)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for p in sys.argv[1:]:
        bisect(p)
