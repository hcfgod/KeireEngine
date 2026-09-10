# Kéire website

The product website, documentation, marketplace, and account pages share an Astro
Node deployment. Run `npm test` and `npm run build` from this directory. The build
syncs canonical documentation, validates local media, generates search, externalizes
inline content for the production CSP, and checks links and document structure.

## Showcase media

`Source/media/outpost` contains rendered output only. The private project generator
and controls are documented in [`Scripts/Examples/Outpost/README.md`](../../../Scripts/Examples/Outpost/README.md).
Do not copy licensed FBX files, textures, or the cooked interactive demo into this site.

The required set is six 1920 × 1080 WebP images (`arrival`, `materials`, `vehicle`,
`effects`, `editor`, `graph`), the main `outpost-tour.mp4`, and `tour-description.vtt`.
The homepage uses still images. The showcase places one optional tour below its image
gallery; the tour preloads metadata so its duration is available before playback.
Earlier short clips remain in source but are excluded from the published media set.
`scripts/showcase-media.mjs` validates the complete set before copying to the generated
`public/assets/outpost` directory. Each still is limited to 600 KB and the film to 18 MB.
The tour is user-initiated and has native controls.
The main film includes a visible text description.

## Visual checks

Preview the built server on an unused loopback port with `HOST=127.0.0.1` and `PORT`
set in the process environment, then run `node dist/server/entry.mjs`. Check desktop
and mobile layouts, keyboard navigation, forms, video playback, documentation search,
and reduced-motion behavior. Protected pages require a configured test account to
verify authenticated content. Do not create fake account or marketplace data for screenshots.

Source identity comes from `Config/Project.conf`; downloadable release identity comes
from the publication metadata. `publication-state.test.ts` verifies these remain
separate when the next source release is ahead of the signed catalog.

Use the existing `../Scripts/deploy-windows-web.ps1` transactional deployment after
validation. Its settings must identify the actual host. Keep its process-ownership
checks and rollback behavior intact. A local preview is not a live deployment.
