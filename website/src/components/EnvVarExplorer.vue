<script setup lang="ts">
import { ref, computed } from "vue";

const vars = [
  {
    name: "OTEL_EXPORTER_OTLP_ENDPOINT",
    scope: "all signals",
    effect: "Base endpoint — each signal's default path is appended (/v1/traces, /v1/metrics, /v1/logs).",
  },
  {
    name: "OTEL_EXPORTER_OTLP_TRACES_ENDPOINT",
    scope: "traces",
    effect: "Full traces endpoint (must carry its own path); wins over the base form.",
  },
  {
    name: "OTEL_EXPORTER_OTLP_METRICS_ENDPOINT",
    scope: "metrics",
    effect: "Full metrics endpoint; wins over the base form.",
  },
  {
    name: "OTEL_EXPORTER_OTLP_LOGS_ENDPOINT",
    scope: "logs",
    effect: "Full logs endpoint; wins over the base form.",
  },
  {
    name: "OTEL_EXPORTER_OTLP_TIMEOUT",
    scope: "all signals",
    effect: "Request budget in ms, applied to both the connect and read phases.",
  },
  {
    name: "OTEL_EXPORTER_OTLP_PROTOCOL",
    scope: "all signals",
    effect: "Must be http/protobuf if set; anything else is a configuration error (INVALID_ARGUMENT).",
  },
  {
    name: "OTEL_EXPORTER_OTLP_HEADERS",
    scope: "all signals",
    effect: "k=v,k=v — extra HTTP headers on every export request (collector auth).",
  },
  {
    name: "OTEL_RESOURCE_ATTRIBUTES",
    scope: "resource",
    effect: "k=v,k=v — resource attributes; a service.name entry yields to OTEL_SERVICE_NAME.",
  },
  {
    name: "OTEL_SERVICE_NAME",
    scope: "resource",
    effect: "The service identity, emitted as service.name on every batch's Resource.",
  },
];

const scopes = ["all", "traces", "metrics", "logs", "resource"];
const scope = ref("all");
const q = ref("");
const shown = computed(() =>
  vars.filter(
    (v) =>
      (scope.value === "all" || v.scope.includes(scope.value)) &&
      (q.value === "" ||
        v.name.toLowerCase().includes(q.value.toLowerCase()) ||
        v.effect.toLowerCase().includes(q.value.toLowerCase())),
  ),
);
</script>

<template>
  <div>
    <div class="mb-5 flex flex-col gap-3 sm:flex-row sm:items-center">
      <div class="flex flex-wrap gap-1.5">
        <button
          v-for="s in scopes"
          :key="s"
          @click="scope = s"
          :class="[
            'rounded-full border px-3 py-1 font-mono text-[11px] transition',
            scope === s
              ? 'border-teal-400 bg-teal-500/15 text-teal-300'
              : 'border-ink-700 text-slate-400 hover:text-slate-200',
          ]"
        >
          {{ s }}
        </button>
      </div>
      <input
        v-model="q"
        placeholder="filter…"
        class="ml-auto w-full rounded-lg border border-ink-700 bg-ink-900 px-3 py-1.5 font-mono text-xs text-slate-200 placeholder-slate-600 focus:border-teal-500/50 focus:outline-none sm:w-44"
      />
    </div>
    <div class="overflow-hidden rounded-xl border border-ink-700">
      <table class="w-full text-left text-sm">
        <thead>
          <tr class="border-b border-ink-700 bg-ink-800/60 font-mono text-[11px] text-slate-400">
            <th class="px-4 py-2.5">Variable</th>
            <th class="px-4 py-2.5">Scope</th>
            <th class="px-4 py-2.5">Effect</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="v in shown"
            :key="v.name"
            class="border-b border-ink-800 last:border-0 hover:bg-ink-800/40"
          >
            <td class="px-4 py-3 font-mono text-[12px] text-teal-300">{{ v.name }}</td>
            <td class="px-4 py-3 font-mono text-[11px] text-slate-500">{{ v.scope }}</td>
            <td class="px-4 py-3 text-slate-400">{{ v.effect }}</td>
          </tr>
          <tr v-if="shown.length === 0">
            <td colspan="3" class="px-4 py-6 text-center text-sm text-slate-500">
              nothing matches
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>
