# TODO 175 — The designed website (Astro 7 / Vite 8 / TW4 / Vue)

**Status:** Complete (v1.1.0)
**Priority:** P1 (the user-facing surface)

## Stack

- Astro 7.2 (static output, /otlp-c base) on Vite 8.2
- Tailwind 4 via @tailwindcss/vite — CSS-first theme (ink/teal
  palette, Inter/JetBrains Mono stacks)
- Vue 3.5 islands via @astrojs/vue 7: CodeTabs (code-sample
  switcher) and AudienceExplorer (constraint filter) — the only
  two client components; everything else is zero-JS static HTML

## Information architecture

- / — hero, stats (133 symbols · 52 tests · 28 gates · 0 deps),
  feature grid, tabbed traces/metrics/logs/env code
- /about — the gap, the constraint, the version arc, license/
  governance/stability cards
- /audiences — six audiences (kernel, firmware, VMs, preload,
  static, security) with an interactive constraint filter
- /use-cases — sidecar edge, VM FFI, library instrumentation,
  collector auth — each with steps + code
- /docs — four consumption modes (FetchContent, add_subdirectory,
  install+find_package, vcpkg overlay), first span, six core
  concepts (signal/tick/emit-move/events/exemplar/freeze),
  build presets
- /examples — minimal, event-loop poll integration, multithreaded
  emit
- /api/ — the Doxygen reference, copied at deploy time (never
  committed); marketing pages + generated reference deploy as one
  artifact so they cannot drift

## Deploy

The Pages job: node 22 (npm cache on website/package-lock.json),
npm ci, astro build, Doxygen into dist/api, upload, deploy.
Local dev: cd website && npm run dev. Verified locally: all six
pages + /api/ serve 200 under the /otlp-c base path.
