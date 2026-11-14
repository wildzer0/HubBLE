#!/usr/bin/env python3
"""Doxygen-style documentation gate for the firmware sources (no Doxygen needed).

Reports every C symbol under firmware/main and firmware/components that has no documentation comment:
functions (definitions and prototypes; every documented function must also have an @param per parameter and an
@return when it returns a value), object-like macros, struct/union fields, enum values and file-scope variables.
It is a heuristic line scanner tuned to this code base (ESP-IDF style, clang-format output), meant as a fast local and CI
gate next to the real Doxygen run (which stays the source of the published API docs).

Usage: tools/check_docs.py [--list] [paths...]   (exit status 1 when something is undocumented)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_PARTS = {"managed_components", "generated", "test", "build"}

FUNC_START = re.compile(r"^[A-Za-z_][\w \t\*]*[\w\*]\s*\(")
DOC_END = re.compile(r"\*/\s*$")
TRAIL_DOC = re.compile(r"/\*\*<|//!<|///<")


def strip_comments_and_strings(line: str) -> str:
    """Remove string literals and trailing // comments so braces inside them are not counted."""
    line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(\\.|[^'\\])'", "''", line)
    return re.sub(r"//.*$", "", line)


def check_file(path: Path) -> list[tuple[int, str]]:
    """Return (line, text) for each undocumented symbol in one file."""
    lines = path.read_text(encoding="utf-8", errors="replace").split("\n")
    problems: list[tuple[int, str]] = []
    depth = 0
    in_block = False
    # Stack of container kinds for brace levels opened by `struct`/`union`/`enum` ("agg") or other ("other").
    stack: list[str] = []
    prev_code = ""  # previous non-blank line, raw
    pending_agg = False
    run_documented = False  # the current run of adjacent #define lines started with a doc comment
    i = 0
    while i < len(lines):
        raw = lines[i]
        s = raw.strip()
        lineno = i + 1
        # Track block comments so their content is never parsed as code.
        if in_block:
            if "*/" in s:
                in_block = False
                prev_code = raw
            i += 1
            continue
        if s.startswith("/*"):
            if "*/" not in s:
                in_block = True
            else:
                prev_code = raw
            i += 1
            continue
        if not s:
            i += 1
            continue
        code = strip_comments_and_strings(raw)
        documented_before = bool(DOC_END.search(prev_code.strip())) or prev_code.strip().startswith("//")
        # A run of consecutive #defines (or members/enum values) shares the comment above the first one.
        if not documented_before and run_documented and prev_code.strip() and (s.startswith("#define") == prev_code.strip().startswith("#define")):
            documented_before = True
        trailing = bool(TRAIL_DOC.search(raw))

        # ---- preprocessor ----
        if s.startswith("#"):
            m = re.match(r"#\s*define\s+(\w+)(\(?)", s)
            if m and not m.group(1).endswith("_H") and not m.group(1).startswith("_") and depth == 0:
                if not (documented_before or trailing):
                    problems.append((lineno, f"macro {m.group(1)}"))
                run_documented = documented_before or trailing or run_documented
            # Skip continuation lines of multi-line macros.
            while lines[i].rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
            prev_code = lines[i]
            i += 1
            continue

        if not s.startswith("#define"):
            run_documented = False
        top = stack[-1] if stack else None

        # ---- aggregate members (struct/union fields, enum values) ----
        # Lines that only continue a previous declaration (prototype tails, initializer ends) are not members.
        continuation = s.endswith((");", "};")) or s == "*/"
        if top == "agg" and not s.startswith("}") and not s.startswith("{") and not continuation:
            if not (documented_before or trailing):
                # A declaration may span lines; look ahead to the terminator for a trailing doc.
                j = i
                while j < len(lines) and not re.search(r"[;,]\s*(/\*\*<.*)?$", lines[j].split("/**<")[0].rstrip() + ";" if False else lines[j]):
                    j += 1
                    if j - i > 6:
                        break
                span = "\n".join(lines[i : min(j + 1, len(lines))])
                if not TRAIL_DOC.search(span):
                    problems.append((lineno, f"member `{s[:50]}`"))

        # ---- functions and file-scope declarations at depth 0 ----
        if depth == 0 and not s.startswith(("typedef", "extern \"C\"", "}", "static_assert", "_Static_assert")):
            if FUNC_START.match(s) and "=" not in s.split("(")[0]:
                name = re.search(r"(\w+)\s*\($", s.split("(")[0] + "(") or re.search(r"(\w+)\s*\(", s)
                # Signature may span lines up to `{` or `;`.
                j = i
                while j < len(lines) and "{" not in lines[j] and ";" not in lines[j]:
                    j += 1
                end = lines[j] if j < len(lines) else ""
                is_def_or_proto = "{" in end or end.rstrip().endswith(";")
                # A static forward declaration is documented on its definition (Doxygen merges them).
                if s.startswith("static") and "{" not in end:
                    is_def_or_proto = False
                if is_def_or_proto and not documented_before:
                    problems.append((lineno, f"function {name.group(1) if name else s[:40]}"))
            elif re.match(r"^static const char \*TAG\b", s):
                pass
            elif re.match(r"^(static |extern |const |volatile |_Atomic )*[A-Za-z_][\w \*]*\s+\**\w+(\[[^\]]*\])*\s*(=.*)?;$", s) and not s.startswith(
                ("return", "typedef")
            ):
                if not (documented_before or trailing):
                    problems.append((lineno, f"variable `{s[:50]}`"))

        # ---- brace accounting ----
        opens = code.count("{")
        closes = code.count("}")
        if opens:
            kind = "agg" if re.match(r"^\s*((typedef|static|const)\s+)*(struct|union|enum)\b[^;()=]*\{\s*$", code) or pending_agg else "other"
            # `typedef struct {` / `enum name {` / `} x = {` initialisers are not aggregates we document.
            if "=" in code:
                kind = "other"
            for _ in range(opens):
                stack.append(kind)
            pending_agg = False
        elif re.match(r"^\s*((typedef|static|const)\s+)*(struct|union|enum)\b\s*\w*\s*$", code):
            pending_agg = True
        for _ in range(closes):
            if stack:
                stack.pop()
        depth = len(stack)
        prev_code = raw
        i += 1
    return problems


TAGGED = re.compile(
    r"(/\*\*(?:(?!\*/).)*?\*/)\s*\n((?:static\s+|inline\s+)*[\w \*]+?\b(\w+)\(([^)]*)\))\s*[{;]", re.S
)


def check_tags(path: Path) -> list[tuple[int, str]]:
    """Return (line, text) for documented functions lacking an `@param` per parameter or an `@return` for a value."""
    text = path.read_text(encoding="utf-8", errors="replace")
    problems: list[tuple[int, str]] = []
    for m in TAGGED.finditer(text):
        doc, sig, name, args = m.group(1), m.group(2), m.group(3), m.group(4).strip()
        if "@copydoc" in doc:
            continue  # inherits everything from another block
        names = []
        for a in args.split(","):
            a = a.strip()
            if a and a != "void":
                found = re.findall(r"(\w+)(?:\[[^\]]*\])?\s*$", a)
                if found:
                    names.append(found[0])
        missing = [n for n in names if not re.search(r"@param(\[\w+\])?\s+" + n + r"\b", doc)]
        returns_value = not re.match(r"^(static\s+|inline\s+)*void\b", sig.strip())
        no_return = returns_value and "@return" not in doc and "@retval" not in doc
        if missing or no_return:
            line = text.count("\n", 0, m.start(2)) + 1
            what = ([f"@param {n}" for n in missing]) + (["@return"] if no_return else [])
            problems.append((line, f"function {name} lacks " + ", ".join(what)))
    return problems


def main(argv: list[str]) -> int:
    paths = [Path(a) for a in argv if not a.startswith("--")]
    if not paths:
        paths = [ROOT / "firmware" / "main", ROOT / "firmware" / "components"]
    files: list[Path] = []
    for p in paths:
        files += [p] if p.is_file() else sorted(p.rglob("*.[ch]"))
    total = 0
    for f in files:
        if SKIP_PARTS & set(f.relative_to(ROOT).parts):
            continue
        probs = check_file(f) + check_tags(f)
        total += len(probs)
        for line, text in probs:
            print(f"{f.relative_to(ROOT)}:{line}: undocumented {text}")
    print(f"{total} undocumented symbol(s) in {len(files)} file(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
