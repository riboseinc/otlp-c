# TODO 178 — FreeBSD gating; the changelog page

**Status:** Complete (v1.1.3)
**Priority:** P2 (CI truth, site completeness)

## FreeBSD: best-effort no more

The job carried `continue-on-error: true` since inception,
annotated "INADDR_LOOPBACK visibility quirk; revisit". Revisited:
the jobs API shows 15/15 green runs on main — the quirk never
reproduced. The flag is removed; FreeBSD 14.2 gates like every
other platform (watch the next few merges).

## Site: /docs/changelog/

The release arc (freeze, exemplars, OTel-native config,
deepening window, the two caught wire bugs) rendered as the
docs sidebar's newest page, linking to the authoritative
CHANGELOG.md. 14 pages.

## Docs truth

CLAUDE.md key-files table gains exporter_sync.c and
exporter_internal.h; test count 52 → 53.
