#!/usr/bin/env python3
"""Packs the game tables page (app/rf-tables.html) into app/vpp-forge.html.

The page goes in gzipped and base64-encoded, in the rfTablesPack script
block, so the app stays one self-contained file (it ships as-is as the
browser build). Run it after editing rf-tables.html; CI runs it before
embedding, so a build always carries the current tables.

    python3 tools/pack_tables.py [app/rf-tables.html] [app/vpp-forge.html]
"""
import base64
import gzip
import re
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "app/rf-tables.html"
dst = sys.argv[2] if len(sys.argv) > 2 else "app/vpp-forge.html"

data = open(src, "rb").read()
b64 = base64.b64encode(gzip.compress(data, compresslevel=9, mtime=0)).decode("ascii")

html = open(dst, encoding="utf-8", newline="").read()
block = re.compile(r'(<script type="application/gzip\+base64" id="rfTablesPack" data-bytes=")\d*(">)[^<]*(</script>)')
out, n = block.subn(lambda m: m.group(1) + str(len(data)) + m.group(2) + b64 + m.group(3), html)
if n != 1:
    sys.exit("pack_tables: no rfTablesPack block in %s" % dst)
if out == html:
    print("pack_tables: %s already current" % dst)
else:
    open(dst, "w", encoding="utf-8", newline="").write(out)
    print("pack_tables: %d bytes -> %d packed into %s" % (len(data), len(b64), dst))
