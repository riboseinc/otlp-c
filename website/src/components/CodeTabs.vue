<script setup lang="ts">
import { ref } from "vue";
defineProps<{
  tabs: { label: string; code: string }[];
}>();
const active = ref(0);
</script>

<template>
  <div
    class="overflow-hidden rounded-xl border border-ink-700 bg-ink-900 shadow-lg shadow-black/30"
  >
    <div class="flex border-b border-ink-700 bg-ink-800/60">
      <button
        v-for="(t, i) in tabs"
        :key="t.label"
        @click="active = i"
        :class="[
          'px-4 py-2 font-mono text-xs transition',
          active === i
            ? 'border-b-2 border-teal-400 text-teal-300'
            : 'border-b-2 border-transparent text-slate-400 hover:text-slate-200',
        ]"
      >
        {{ t.label }}
      </button>
    </div>
    <pre
      class="overflow-x-auto p-5 font-mono text-[13px] leading-relaxed text-slate-300"
    ><code>{{ tabs[active].code }}</code></pre>
  </div>
</template>
