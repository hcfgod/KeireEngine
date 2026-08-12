import { defineConfig } from "astro/config";
import node from "@astrojs/node";
import sitemap from "@astrojs/sitemap";
import starlight from "@astrojs/starlight";

import { docGroups, sourcePathToSlug } from "./doc-library.mjs";

const documentationSidebar = docGroups.map(({ label, files }) => ({
    label,
    collapsed: label !== "Start here",
    items: files.map((sourcePath) => ({ slug: `docs/${sourcePathToSlug(sourcePath)}` })),
}));

export default defineConfig({
    site: "https://keireengine.duckdns.org",
    srcDir: "./Source",
    output: "server",
    adapter: node({ mode: "standalone" }),
    trailingSlash: "always",
    security: {
        // Astro compares Origin with the loopback request URL before middleware can
        // account for Caddy's trusted forwarding headers. Every mutation is instead
        // protected by the stricter canonical-origin check in Source/middleware.ts.
        checkOrigin: false,
    },
    build: {
        inlineStylesheets: "never",
    },
    vite: {
        build: {
            assetsInlineLimit: 0,
        },
    },
    integrations: [
        sitemap(),
        starlight({
            title: "Kéire Engine Docs",
            titleDelimiter: "—",
            description: "Production documentation for Kéire Engine, its Hub, editor, runtime, scripting, content pipeline, diagnostics, and release workflows.",
            favicon: "/assets/keire.png",
            logo: {
                src: "../Website/assets/keire.png",
                alt: "Kéire Engine",
            },
            social: [
                {
                    icon: "external",
                    label: "Kéire Engine website",
                    href: "/",
                },
                {
                    icon: "github",
                    label: "Kéire Engine on GitHub",
                    href: "https://github.com/hcfgod/KeireEngine",
                },
            ],
            sidebar: [
                {
                    label: "Kéire Engine",
                    items: [
                        { label: "Documentation home", slug: "docs" },
                    ],
                },
                ...documentationSidebar,
            ],
            pagefind: true,
            pagination: true,
            lastUpdated: false,
            credits: false,
            customCss: ["./Source/styles/keire.css"],
            components: {
                Header: "./Source/components/DocsHeader.astro",
                SiteTitle: "./Source/components/DocsSiteTitle.astro",
                MobileMenuFooter: "./Source/components/DocsMobileMenuFooter.astro",
            },
            head: [
                {
                    tag: "meta",
                    attrs: {
                        property: "og:image",
                        content: "https://keireengine.duckdns.org/assets/hero-cinematic.png",
                    },
                },
                {
                    tag: "meta",
                    attrs: {
                        name: "theme-color",
                        content: "#050915",
                    },
                },
            ],
        }),
    ],
});
