import type { APIRoute } from "astro";

import { releaseNotes } from "../../lib/changelog";

export const prerender = true;

function escapeXml(value: string): string {
    return value
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&apos;");
}

export const GET: APIRoute = ({ site }) => {
    const origin = site ?? new URL("https://keireengine.duckdns.org");
    const items = releaseNotes.filter((release) => release.published).map((release) => {
        const link = new URL(`/changelog/${release.version}/`, origin).toString();
        const publicationDate = release.releaseDate
            ? new Date(`${release.releaseDate}T00:00:00Z`).toUTCString()
            : new Date(0).toUTCString();
        return [
            "<item>",
            `<title>${escapeXml(`Kéire ${release.version}`)}</title>`,
            `<link>${escapeXml(link)}</link>`,
            `<guid isPermaLink="true">${escapeXml(link)}</guid>`,
            `<pubDate>${publicationDate}</pubDate>`,
            `<description>${escapeXml(release.summary)}</description>`,
            "</item>",
        ].join("");
    }).join("");
    const self = new URL("/changelog/rss.xml", origin).toString();
    return new Response([
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<rss version="2.0"><channel>',
        "<title>Kéire Engine changelog</title>",
        `<link>${escapeXml(new URL("/changelog/", origin).toString())}</link>`,
        "<description>Versioned Kéire Engine release notes and platform updates.</description>",
        `<atom:link xmlns:atom="http://www.w3.org/2005/Atom" href="${escapeXml(self)}" rel="self" type="application/rss+xml"/>`,
        items,
        "</channel></rss>",
    ].join(""), { headers: { "Content-Type": "application/rss+xml; charset=utf-8" } });
};
