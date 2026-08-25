import { defineConfig } from 'astro/config';
import vue from '@astrojs/vue';
import tailwindcss from '@tailwindcss/vite';

// The site serves from the project pages path: riboseinc.github.io/otlp-c/
export default defineConfig({
  site: 'https://riboseinc.github.io',
  base: '/otlp-c',
  integrations: [vue()],
  vite: {
    plugins: [tailwindcss()],
  },
});
