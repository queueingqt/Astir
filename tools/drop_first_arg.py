"""Drop the first argument from calls to named functions.

The regex version handled `fn(w, ...)` but not `fn(SL_da[type][row], ...)` --
the first argument can be any expression.  Parse the argument list with balanced
parens and brackets instead, and remove everything up to the first top-level
comma.  Only called for functions whose leading Widget has just been removed
from the signature, so the first argument is always the one to drop.
"""
import sys, re

def drop(text, fn):
    out, i, n, count = [], 0, len(text), 0
    pat = re.compile(r'\b%s\s*\(' % re.escape(fn))
    while True:
        m = pat.search(text, i)
        if not m:
            out.append(text[i:]); break
        open_at = m.end() - 1
        depth, j = 0, open_at
        first_comma = -1
        while j < n:
            c = text[j]
            if c in '([':
                depth += 1
            elif c in ')]':
                depth -= 1
                if depth == 0:
                    break
            elif c == ',' and depth == 1:
                first_comma = j
                break
            j += 1
        if first_comma < 0:
            out.append(text[i:m.end()]); i = m.end(); continue
        # Skip definitions and declarations.  The stripper cannot tell them
        # from a call.  A first attempt tested whether the NAME started in
        # column 0, which never fires -- a definition begins with its return
        # type, so the name is always indented.  Test the two things that
        # actually distinguish them: a definition has '{' after the closing
        # paren, and a declaration has only a type and qualifiers before the
        # name on its line.
        close = j          # index of the balanced ')' found below, recomputed here
        depth2, k2 = 0, open_at
        while k2 < n:
            if text[k2] in '([':
                depth2 += 1
            elif text[k2] in ')]':
                depth2 -= 1
                if depth2 == 0:
                    break
            k2 += 1
        after = k2 + 1
        while after < n and text[after] in ' \t\r\n':
            after += 1
        line_start = text.rfind("\n", 0, m.start()) + 1
        before = text[line_start:m.start()]
        is_def = after < n and text[after] == '{'
        is_decl = re.match(r'^\s*(?:static\s+|extern\s+|const\s+)*[A-Za-z_]\w*\s*\**\s*$', before) is not None
        if is_def or is_decl:
            out.append(text[i:m.end()]); i = m.end(); continue
        out.append(text[i:open_at + 1])
        # skip the first argument and the comma, plus following whitespace
        k = first_comma + 1
        while k < n and text[k] in ' \t':
            k += 1
        i = k
        count += 1
    return ''.join(out), count

if __name__ == "__main__":
    path, fns = sys.argv[1], sys.argv[2].split(",")
    t = open(path, encoding="utf-8").read()
    tot = 0
    for fn in fns:
        t, k = drop(t, fn)
        tot += k
    open(path, "w", encoding="utf-8").write(t)
    print("  %-24s dropped %d first args" % (path, tot))
