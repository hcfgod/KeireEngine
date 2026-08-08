# Third-party notices

The offline Kéire distribution publisher uses:

- NSec.Cryptography 26.4.0, licensed under MIT. See `Licenses/NSec.Cryptography.txt` and
  `Licenses/NSec.Cryptography.NOTICE.txt`.
- libsodium 1.0.22, licensed under ISC. See `Licenses/libsodium.txt`.

These dependencies are private implementation details of the publisher and are not part of the distribution HTTP
contract.

The packaged static documentation site includes output produced by:

- Astro 7.2.0, licensed under MIT. See `Licenses/Astro.txt`.
- Starlight 0.41.7, licensed under MIT. See `Licenses/Starlight.txt`.
- Expressive Code 0.44.1, licensed under MIT. See `Licenses/ExpressiveCode.txt`.
- Pagefind 1.5.2, including its default UI, licensed under MIT. See `Licenses/Pagefind.txt`.
- Beautiful Mermaid 1.1.3, used at build time to render documentation diagrams and licensed under MIT. See
  `Licenses/BeautifulMermaid.txt`.

These packages are pinned by `DocumentationSite/package-lock.json`. Node.js and npm are build-time requirements and
are not included in the distribution-service archive.
