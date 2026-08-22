# TODO 134 — Public-header API coherence audit

**Status:** Complete (v0.5.94)
**Priority:** P2 (pre-0.6.0 API review)

## What shipped

Systematic review of every public header (`include/otlp-c/*.h`):
naming vocabulary, ownership documentation, return-code
documentation, umbrella completeness, include hygiene.

**Finding 1 (real bug): duplicate declarations in `metric.h`.**
`otlp_metric_set_attribute_array` / `_kvlist` were declared
TWICE — a copy-paste artifact from the v0.5.71 parity patch.
Harmless to the compiler but a public-header defect shipped for
23 releases. Removed; a duplicate-declaration scan across all
headers now comes back clean.

**Finding 2: return-code documentation was exporter-only.**
`metric.h` and `log.h` (28 status-returning functions combined)
documented no return codes at the header level — only the
exporter did. Both headers now carry a uniform "Return codes"
note: OK / NULL / NOMEM / OVERFLOW / INVALID_ARGUMENT, plus the
per-surface specifics (all-zero IDs, wrong-type operations).

**Verified clean as-is** (negative results):
- Naming vocabulary uniform (`otlp_<type>_<verb>` throughout;
  set/add/mark verbs consistent).
- Every create/free pair documented with lifetime language
  ("caller-owned") in every header except visibility.h (a macro
  header — correctly exempt).
- Umbrella `otlp.h` includes all 13 includable headers.
- Include hygiene: every header compiles standalone as C99 AND
  as C++ (extern "C" guards correct) — both verified by
  mechanical compile sweeps.

## Verification

Debug + Release suites green (39/39) after the header edits;
header standalone-compile sweeps clean in both languages.
