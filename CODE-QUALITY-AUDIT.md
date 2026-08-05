# Code quality audit — v0.2.x

Self-audit against OOP / MECE / OCP / DRY / performance / semantic
clarity principles. Findings + actions taken.

## Methodology

Walked every source file in `src/` and `include/otlp-c/`. For each
public function and struct, checked:

- **MECE**: does it own exactly one concern? Does it overlap with
  another module?
- **OCP**: would adding a new variant (new attribute type, new
  message field, new transport) require modifying existing code, or
  only adding new code?
- **DRY**: are there duplicate helpers, copy-pasted patterns, or
  near-identical functions?
- **Performance**: hot-path allocations, redundant scans, locks.
- **Semantic clarity**: do names match domain concepts? Are there
  ambiguous abbreviations?

## Findings

### MECE — module boundaries are clean

| Module | Owns | Doesn't own |
|---|---|---|
| `common.c` | version + strerror | anything stateful |
| `internal_util.c` | dup_str / dup_bytes | anything else |
| `arena.c` | bump allocator | encoder logic |
| `protobuf_encode.c` | wire primitives | OTLP semantics |
| `otlp_messages.c` | OTLP message encoders | wire primitives |
| `otlp_schema.h` | field tables | encoders |
| `span.c` | span lifecycle | encoding |
| `tracer.c` | ID generation | span storage |
| `http_client.c` | HTTP state machine | URL semantics |
| `platform_{unix,win}.c` | socket primitives | HTTP |
| `mpsc_queue.c` | lock-free queue | exporter state |
| `exporter.c` | batching + tick | encoding |
| `exporter_otel.c` | encode + start POST | batching |

No overlaps found. Each file owns exactly one concern.

### OCP — partial

**Open for extension:**
- Schema tables in `otlp_schema.h` — adding a field is one line.
- `otlp_exporter_opts_t` — adding an option is additive.
- Span setters — adding `otlp_span_add_event` / `_link` is additive
  (TODO 13 stubs).

**Closed against modification (still needs work):**
- `struct otlp_attribute` is a closed enum + union. Adding a new
  attribute type (e.g. ArrayValue) means editing `enum otlp_attr_type`,
  the union, every setter, every encoder switch. This is the
  remaining OCP gap, tracked in TODO 43.

### DRY — converged

Previous duplicates (`dup_str` × 3 in src/, `decode_varint` × 3 in
tests/) were extracted to `internal_util.{h,c}` and
`tests/property/decoder.h` respectively. No remaining duplicates
identified.

The schema constants in `otlp_messages.c` are now table-backed
(derived from `otlp_schema.h`); the previous hand-rolled `#define`
duplication is gone.

### Performance — current baselines

From `bench/bench_encode.c` on Apple M-series:

| Workload | Throughput |
|---|---|
| 1 span × 1 attr | 2.0M spans/sec |
| 100 spans × 1 attr | 2.3M spans/sec |
| 100 spans × 8 attrs | 0.6M spans/sec |
| 100 spans × 32 attrs | 0.17M spans/sec |

Hot path: span creation + attribute append + emit + encode + POST.
For typical use (1-8 attributes per span), the library sustains
>500K spans/sec single-threaded. The arena primitive (TODO 14)
is not yet wired into the encoder; doing so would eliminate
per-submessage malloc and likely 2× the encode throughput.

### Semantic clarity — good

Public API names match OTLP domain concepts: `otlp_span_*`,
`otlp_tracer_*`, `otlp_exporter_*`. Internal accessors are prefixed
with `otlp_` consistently. No cryptic abbreviations in the public
surface.

## Recommendations

1. **TODO 43 (model-driven attributes)** — the remaining OCP gap.
   v0.3+ work.
2. **TODO 14 finish (arena in encoder)** — 2× encode speedup
   expected. v0.3+ work.
3. **TODO 42 (slab allocator)** — for firehose use cases (>100K
   spans/sec sustained). v0.3+ work.
4. **External code review** — scheduled for v1.0 (Ribose security
   team).

## Verdict

The codebase is in a maintainable state for v0.2.x. The three
remaining improvements (43, 14, 42) are tracked and non-blocking.
