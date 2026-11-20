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
            {
              text: 'Features',
              link: '/guides/usage-and-features/',
              collapsed: true,
              items: [
                { text: 'Tweaks', link: '/guides/usage-and-features/tweaks' },
                { text: 'Safety and tiers', link: '/guides/usage-and-features/safety-and-tiers' },
                { text: 'System info and window', link: '/guides/usage-and-features/system-info-and-gui' },
                { text: 'Daemon and CLI', link: '/guides/usage-and-features/daemon-and-cli' },
              ],
            },
            { text: 'Install and uninstall', link: '/guides/install-and-uninstall' },
            { text: 'Build and test', link: '/guides/build-and-test' },
          ],
        },
        {
          text: 'How it works',
          items: [
            { text: 'Vision and principles', link: '/guides/vision-and-principles' },
            { text: 'Universal pattern', link: '/guides/universal-pattern' },
            {
              text: 'Hardware and tiers',
              link: '/guides/hardware-and-tiers/',
              collapsed: true,
              items: [
                { text: 'APU limits', link: '/guides/hardware-and-tiers/apu-limits' },
                { text: 'Tiers and guards', link: '/guides/hardware-and-tiers/tiers-and-guards' },
                { text: 'Design: classification', link: '/guides/hardware-and-tiers/design-spec-classification' },
                { text: 'Design: integration', link: '/guides/hardware-and-tiers/design-spec-integration' },
              ],
            },
            { text: 'Rust and C++ core', link: '/guides/rust-cpp-core' },
            {
              text: 'Modules',
              link: '/guides/modules/',
              collapsed: true,
              items: [
                { text: 'Code layers', link: '/guides/modules/layers' },
                { text: 'SystemInfo', link: '/guides/modules/system-info' },
                { text: 'Repository tree', link: '/guides/modules/repo-tree' },
              ],
            },
            { text: 'Security', link: '/guides/security' },
            { text: 'ADR 0001: Rust decides, C++ acts', link: '/adr/0001-nucleo-que-decide-y-puertos' },
          ],
        },
        {
          text: 'Module by module',
          items: [
            { text: 'app/: start-up and modes', link: '/guides/how-it-works-app' },
            { text: 'session/: use cases', link: '/guides/how-it-works-session' },
            { text: 'optimize/: the levers', link: '/guides/how-it-works-optimize' },
            { text: 'windows/: Windows calls', link: '/guides/how-it-works-windows' },
            { text: 'ui/: the window', link: '/guides/how-it-works-ui' },
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
