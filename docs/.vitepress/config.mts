import { defineConfig } from 'vitepress'

const title = 'VegaFlow Universal'
const description = 'A Windows daemon that frees CPU and GPU time for games on AMD APUs, without touching anti-cheat games.'
const repo = 'https://github.com/partprogramming09/vegaflow-universal'

export default defineConfig({
  lang: 'en',
  title,
  description,
  cleanUrls: true,
  // Plans, research and handoff notes are working material: never published
  srcExclude: ['plans/**', 'research/**', 'estado.md'],
  lastUpdated: true,
  head: [
    ['meta', { property: 'og:title', content: title }],
    ['meta', { property: 'og:description', content: description }],
  ],
  themeConfig: {
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Guides', link: '/guides/' },
      { text: 'Measurements', link: '/measurements/verification-status' },
      { text: 'Contributing', link: `${repo}/blob/main/CONTRIBUTING.md` },
    ],
    socialLinks: [{ icon: 'github', link: repo }],
    sidebar: {
      '/guides/': [
        {
          text: 'Start here',
          items: [
            { text: 'What VegaFlow is', link: '/guides/' },
            { text: 'Features', link: '/guides/usage-and-features' },
            { text: 'Install and uninstall', link: '/guides/install-and-uninstall' },
            { text: 'Build and test', link: '/guides/build-and-test' },
          ],
        },
        {
          text: 'How it works',
          items: [
            { text: 'Vision and principles', link: '/guides/vision-and-principles' },
            { text: 'Universal pattern', link: '/guides/universal-pattern' },
            { text: 'Hardware and tiers', link: '/guides/hardware-and-tiers' },
            { text: 'Rust and C++ core', link: '/guides/rust-cpp-core' },
            { text: 'Modules', link: '/guides/modules' },
            { text: 'Security', link: '/guides/security' },
            { text: 'ADR 0001: Rust decides, C++ acts', link: '/adr/0001-nucleo-que-decide-y-puertos' },
          ],
        },
      ],
      '/adr/': [{ text: 'Decisions', items: [{ text: 'ADR 0001', link: '/adr/0001-nucleo-que-decide-y-puertos' }] }],
      '/measurements/': [
        {
          text: 'Measurements',
          items: [
            { text: 'Verification status', link: '/measurements/verification-status' },
            { text: '2026-11 baseline', link: '/measurements/2026-11-base' },
            { text: '2026-11 A/B daemon', link: '/measurements/2026-11-ab-daemon' },
            { text: '2026-11-05 real games', link: '/measurements/2026-11-juegos-reales' },
            { text: '2026-11 Octopath II factorial', link: '/measurements/2026-11-octopath2-factorial' },
            { text: '2026-11-09 laptop diagnosis', link: '/measurements/2026-11-09-diagnostico-laptop' },
            { text: '2026-11-14 package 0.1.0', link: '/measurements/2026-11-14-paquete-0.1.0' },
            { text: '2026-11-15 Octopath regression', link: '/measurements/2026-11-15-regresion-octopath' },
          ],
        },
      ],
    },
    search: { provider: 'local' },
    editLink: { pattern: `${repo}/edit/main/docs/:path` },
  },
})
