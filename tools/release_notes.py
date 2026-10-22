#!/usr/bin/env python3
"""Writes the GitHub release notes for the version in src/version.h, from
its What's new entry (the CHANGES list in app/vpp-forge.html) and the
SHA-256 of the files the release ships, so no release page goes out blank.
The update window's What's new button opens that page.

    python3 tools/release_notes.py OUT.md [TAG]

With TAG (the pushed tag, e.g. v1.23.3) it fails unless the tag names the
version in src/version.h.
"""
import hashlib
import json
import re
import sys

REPO = "https://github.com/RomekRF/VPPForge"
SHIPPED = [("VPPForge-Desktop-Win64.zip", "dist/VPPForge-Desktop-Win64.zip"),
           ("VPPForge-Browser.html", "dist/VPPForge-Browser.html"),
           ("vppforge.exe", "vppforge.exe")]


def js_strings_and_brackets(src):
    """Tokens of a JS array literal: '[', ']', or a decoded "..." string."""
    out, i = [], 0
    while i < len(src):
        ch = src[i]
        if ch in "[]":
            out.append(ch)
            i += 1
        elif ch == '"':
            j = i + 1
            while src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            out.append(json.loads(src[i:j + 1].replace("\\'", "'")))
            i = j + 1
        else:
            i += 1
    return out


def changes(html):
    """The CHANGES entries, newest first: (version, date, [(line, note)])."""
    start = html.index("const CHANGES=[")
    entries = []
    for m in re.finditer(r'\{v:"([^"]+)",d:"(\d{4}-\d\d-\d\d)",c:\[', html[start:]):
        body_at = start + m.end()
        depth, k = 1, body_at
        while depth:        # find the c:[ ... ] close, skipping strings
            if html[k] == '"':
                k += 1
                while html[k] != '"':
                    k += 2 if html[k] == "\\" else 1
            elif html[k] == "[":
                depth += 1
            elif html[k] == "]":
                depth -= 1
            k += 1
        toks = js_strings_and_brackets(html[body_at:k - 1])
        items, pair = [], None
        for t in toks:
            if t == "[":
                pair = []
            elif t == "]":
                items.append((pair[0], pair[1] if len(pair) > 1 else ""))
                pair = None
            elif pair is not None:
                pair.append(t)
            else:
                items.append((t, ""))
        entries.append((m.group(1), m.group(2), items))
    return entries


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def main():
    out = sys.argv[1]
    ver = re.search(r'#define VPP_VERSION\s+"([^"]+)"', open("src/version.h", encoding="utf-8").read()).group(1)
    if len(sys.argv) > 2 and sys.argv[2].lstrip("vV") != ver:
        sys.exit("release_notes: tag %s does not match version.h %s" % (sys.argv[2], ver))
    entries = changes(open("app/vpp-forge.html", encoding="utf-8").read())
    if not entries or entries[0][0] != ver:
        sys.exit("release_notes: What's new does not start at %s" % ver)
    _, _, items = entries[0]
    prev = entries[1][0] if len(entries) > 1 else None

    md = ["The Red Faction VPP workbench, by Romek (RomekRF).", "", "## New in %s" % ver, ""]
    for line, note in items:
        md += ["**%s**%s" % (line, " " + note if note else ""), ""]
    md += ["**Updating.** If you are on 1.22.1 or newer, VPP Forge tells you this version is out, "
           "and after updating, **Help › What's new** lists every change. On 1.20.0 through 1.22.0 "
           "the notice is easy to miss, so use **Help › Check for updates**. On 1.19.0 or earlier, "
           "download it from this page.", ""]
    if prev:
        md += ["Everything in [%s](%s/releases/tag/v%s) is included." % (prev, REPO, prev), ""]
    md += ["## Two ways to run it", "",
           "**`VPPForge-Desktop-Win64.zip`** — the desktop app. Unzip and run `vppforge.exe`. "
           "Installing is optional: the first launch offers to associate `.vpp` files and add a "
           "Start Menu entry, and declining just runs the app.", "",
           "**`VPPForge-Browser.html`** — the whole app in a single file. Open it in Chrome or Edge. "
           "Nothing to install and nothing for antivirus to scan. You only lose what needs Windows "
           "itself: files open by drag-and-drop rather than double-click, saving downloads a new "
           "`.vpp` instead of writing back over the original, and linking other VPPs for previews "
           "needs the desktop app.", "",
           "`vppforge.exe` is also published on its own. It is the same file that lives inside the "
           "zip, and it is what the in-app updater downloads.", "",
           "## Before you run it", "",
           "SmartScreen or Defender may warn on first launch because the exe is not code-signed. "
           "Choose \"More info\", then \"Run anyway\". The browser build avoids this entirely.", "",
           "SHA-256", "```"]
    md += ["%-27s %s" % (name, sha256(path)) for name, path in SHIPPED]
    md += ["```", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md))
    print("release_notes: %s, %d changes, written to %s" % (ver, len(items), out))


if __name__ == "__main__":
    main()
