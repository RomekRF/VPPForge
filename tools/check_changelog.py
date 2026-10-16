#!/usr/bin/env python3
"""Fails the build when What's new does not start at the version in
src/version.h, so a release never ships without its notes. The notes are
the CHANGES list in app/vpp-forge.html, newest first.

    python3 tools/check_changelog.py
"""
import re
import sys

ver = re.search(r'#define VPP_VERSION\s+"([^"]+)"', open("src/version.h", encoding="utf-8").read()).group(1)
html = open("app/vpp-forge.html", encoding="utf-8").read()
top = re.search(r'const CHANGES=\[\s*\{v:"([^"]+)",d:"(\d{4}-\d\d-\d\d)"', html)
if not top:
    sys.exit("check_changelog: no CHANGES list in app/vpp-forge.html")
if top.group(1) != ver:
    sys.exit("check_changelog: What's new starts at %s but version.h says %s; add the notes for %s"
             % (top.group(1), ver, ver))
print("check_changelog: %s has notes (%s)" % (ver, top.group(2)))
