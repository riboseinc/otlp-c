<script setup lang="ts">
import { ref, computed } from "vue";

const audiences = [
  {
    icon: "🐧",
    name: "Kernel modules",
    constraint: "No C++ available",
    detail:
      "Kernel-space code cannot link a C++ runtime, period. otlp-c compiles with a bare C11 toolchain — the same one you build the module with — and never spawns a thread, so it is safe to tick from your own kthread.",
    tags: ["kernel", "no-threads"],
  },
  {
    icon: "📟",
    name: "Embedded firmware",
    constraint: "No C++ runtime, tight memory",
    detail:
      "A single static archive with zero third-party deps: the protobuf encoder and the HTTP/1.1 client are hand-rolled. sizeof(otlp_span) is 176 bytes; emit costs ~89 ns.",
    tags: ["kernel", "embedded", "small"],
  },
  {
    icon: "🐍",
    name: "Language runtimes",
    constraint: "No C++ dep in the host process",
    detail:
      "CPython, Ruby MRI, Lua: embedding a C++ runtime inside a VM invites ABI and GC trouble. otlp-c is pure C with an opaque handle surface designed for FFI bindings.",
    tags: ["vm", "ffi"],
  },
  {
    icon: "💉",
    name: "Libc-preloaded tracers",
    constraint: "Must stay buildable with cc only",
    detail:
      "Tracers injected via LD_PRELOAD cannot assume the host ever built anything with g++. One .c-compiled static archive, deep-copied opts, no constructor side effects.",
    tags: ["preload", "tracing"],
  },
  {
    icon: "📦",
    name: "Static binaries",
    constraint: "No C++ standard library to link",
    detail:
      "Fully static executables (distroless, scratch containers, recovery tools) cannot carry libstdc++. otlp-c links into a musl static build with nothing but libc.",
    tags: ["static", "containers"],
  },
  {
    icon: "🔐",
    name: "Security-critical code",
    constraint: "Minimal attack surface",
    detail:
      "No protobuf runtime, no async framework, no templates. Hand-rolled encoders mean every line is auditable; CWE-93/190 hardening is mutation-tested in CI.",
    tags: ["audit", "hardened"],
  },
];

const filter = ref("all");
const tags = ["all", ...new Set(audiences.flatMap((a) => a.tags))];
const shown = computed(() =>
  filter.value === "all"
    ? audiences
    : audiences.filter((a) => a.tags.includes(filter.value)),
);
</script>

<template>
  <div>
    <div class="mb-6 flex flex-wrap gap-2">
      <button
        v-for="t in tags"
        :key="t"
        @click="filter = t"
        :class="[
          'rounded-full border px-3 py-1 font-mono text-xs transition',
          filter === t
            ? 'border-teal-400 bg-teal-500/15 text-teal-300'
            : 'border-ink-700 text-slate-400 hover:border-slate-500 hover:text-slate-200',
        ]"
      >
        {{ t }}
      </button>
    </div>
    <div class="grid gap-4 sm:grid-cols-2">
      <div
        v-for="a in shown"
        :key="a.name"
        class="rounded-xl border border-ink-700 bg-ink-900/60 p-6 transition hover:border-teal-500/40"
      >
        <div class="mb-3 flex items-center gap-3">
          <span class="text-2xl">{{ a.icon }}</span>
          <div>
            <h3 class="font-semibold text-slate-100">{{ a.name }}</h3>
            <p class="font-mono text-xs text-amber-300/80">{{ a.constraint }}</p>
          </div>
        </div>
        <p class="text-sm leading-relaxed text-slate-400">{{ a.detail }}</p>
      </div>
    </div>
  </div>
</template>
