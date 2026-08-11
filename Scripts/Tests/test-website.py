#!/usr/bin/env python3
"""Validate the static Kéire website's production and trust contracts."""

from __future__ import annotations

from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit
import json
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
WEBSITE = ROOT / "Services" / "KeireDistributionService" / "Website"
PUBLIC_ROUTES = {
    "/",
    "/features/",
    "/docs/",
    "/downloads/",
    "/downloads/previous/",
    "/roadmap/",
    "/contact/",
    "/privacy/",
}


class PageParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.links: list[tuple[str, str]] = []
        self.ids: set[str] = set()
        self.h1_count = 0
        self.has_main = False
        self.has_description = False
        self.has_title = False
        self.images: list[dict[str, str | None]] = []
        self.inline_scripts = 0
        self.inline_styles = 0
        self._in_title = False

    def handle_starttag(
        self, tag: str, attributes: list[tuple[str, str | None]]
    ) -> None:
        values = dict(attributes)
        if "id" in values and values["id"]:
            if values["id"] in self.ids:
                raise ValueError(f"Duplicate HTML id: {values['id']}")
            self.ids.add(values["id"])
        if tag == "a" and values.get("href"):
            self.links.append(("href", str(values["href"])))
        if tag in {"link", "script", "img"}:
            attribute = "href" if tag == "link" else "src"
            if values.get(attribute):
                self.links.append((attribute, str(values[attribute])))
        if tag == "img":
            self.images.append(values)
        if tag == "h1":
            self.h1_count += 1
        if tag == "main":
            self.has_main = True
        if (
            tag == "meta"
            and values.get("name") == "description"
            and values.get("content")
        ):
            self.has_description = True
        if tag == "script" and not values.get("src"):
            self.inline_scripts += 1
        if tag == "style" or "style" in values:
            self.inline_styles += 1
        if tag == "title":
            self._in_title = True

    def handle_endtag(self, tag: str) -> None:
        if tag == "title":
            self._in_title = False

    def handle_data(self, data: str) -> None:
        if self._in_title and data.strip():
            self.has_title = True


def target_file(page: Path, value: str) -> tuple[Path, str]:
    parsed = urlsplit(value)
    path = parsed.path
    if not path:
        return page, parsed.fragment
    if path.startswith("/"):
        candidate = WEBSITE / path.lstrip("/")
    else:
        candidate = page.parent / path
    if path.endswith("/") or candidate.is_dir():
        candidate /= "index.html"
    return candidate.resolve(), parsed.fragment


def validate_page(page: Path) -> None:
    parser = PageParser()
    parser.feed(page.read_text(encoding="utf-8"))
    if (
        parser.h1_count != 1
        or not parser.has_main
        or not parser.has_title
        or not parser.has_description
    ):
        raise ValueError(f"Page metadata or landmark contract failed: {page}")
    if parser.inline_scripts or parser.inline_styles:
        raise ValueError(f"Inline script or style violates the website CSP: {page}")
    for image in parser.images:
        if "alt" not in image or not image.get("width") or not image.get("height"):
            raise ValueError(
                f"Image accessibility or layout metadata is incomplete: {page}"
            )
    for _, value in parser.links:
        parsed = urlsplit(value)
        if parsed.scheme:
            if parsed.scheme != "https":
                raise ValueError(f"Non-HTTPS external resource in {page}: {value}")
            continue
        if value.startswith("//"):
            raise ValueError(f"Protocol-relative resource in {page}: {value}")
        if (
            page == WEBSITE / "docs" / "index.html"
            and parsed.path.startswith("/docs/reference/")
        ):
            # The production documentation build overlays these generated routes during packaging.
            continue
        target, fragment = target_file(page, value)
        try:
            target.relative_to(WEBSITE.resolve())
        except ValueError as error:
            raise ValueError(
                f"Website link escapes its root in {page}: {value}"
            ) from error
        if not target.is_file():
            raise ValueError(f"Broken website link in {page}: {value}")
        if fragment and target.suffix == ".html":
            target_parser = PageParser()
            target_parser.feed(target.read_text(encoding="utf-8"))
            if fragment not in target_parser.ids:
                raise ValueError(f"Missing fragment target in {page}: {value}")


def main() -> int:
    pages = sorted(WEBSITE.rglob("*.html"))
    if len(pages) != 9:
        raise ValueError(
            "Website must contain eight public pages and one branded 404 page."
        )
    for page in pages:
        validate_page(page)

    docs_landing = (WEBSITE / "docs" / "index.html").read_text(encoding="utf-8")
    legacy_docs_prefixes = (
        "https://github.com/hcfgod/KeireEngine/blob/master/Docs",
        "https://github.com/hcfgod/KeireEngine/tree/master/Docs",
    )
    if any(prefix in docs_landing for prefix in legacy_docs_prefixes):
        raise ValueError(
            "Documentation navigation must retain first-party /docs/ routes instead of GitHub guide links."
        )
    docs_parser = PageParser()
    docs_parser.feed(docs_landing)
    native_doc_links = [
        value
        for _, value in docs_parser.links
        if value.startswith("/docs/reference/")
    ]
    if len(native_doc_links) < 55:
        raise ValueError(
            "Documentation fallback must retain first-party routes for every guide card."
        )

    manifest = json.loads((WEBSITE / "site.webmanifest").read_text(encoding="utf-8"))
    if (
        manifest.get("start_url") != "/"
        or manifest.get("icons", [{}])[0].get("src") != "/assets/keire.png"
    ):
        raise ValueError("Website manifest identity is invalid.")
    sitemap = ET.parse(WEBSITE / "sitemap.xml")
    namespace = {"site": "http://www.sitemaps.org/schemas/sitemap/0.9"}
    routes = {
        urlsplit(str(value.text)).path
        for value in sitemap.findall("site:url/site:loc", namespace)
        if value.text
    }
    if routes != PUBLIC_ROUTES:
        raise ValueError("Website sitemap routes are incomplete or unexpected.")

    downloads = (WEBSITE / "assets" / "downloads.js").read_text(encoding="utf-8")
    for contract in (
        "hubInstaller",
        "/v2/catalog/",
        "/v1/packages/",
        "/preview-downloads/",
        "windows",
        "macos",
        "linux",
        "x86_64",
        "arm64",
        "editorVersion",
        "publishedAt",
        "publishedTimestamp",
        "timeZoneName: \"short\"",
        "data-download-history",
    ):
        if contract not in downloads:
            raise ValueError(f"Downloads implementation is missing '{contract}'.")
    if "http://" in downloads or "https://" in downloads:
        raise ValueError("Downloads must not use a separate or untrusted origin.")
    if 'timeZone: "UTC"' in downloads:
        raise ValueError("Download publication times must use the viewer's local timezone.")

    styles = (WEBSITE / "assets" / "site.css").read_text(encoding="utf-8")
    for contract in (
        ".download-card {\n    display: flex;\n    min-width: 0;",
        ".download-variants {\n    display: grid;\n    min-width: 0;",
        ".download-variant {\n    width: 100%;\n    min-width: 0;\n    max-width: 100%;",
        "flex: 1 1 0;\n    width: 0;\n    min-width: 0;",
        "white-space: normal;",
    ):
        if contract not in styles:
            raise ValueError(
                "Download cards must constrain long controls and checksums without clipping."
            )

    previews = json.loads(
        (WEBSITE / "assets" / "preview-downloads.json").read_text(encoding="utf-8")
    )
    packages = previews.get("packages", [])
    if previews.get("schemaVersion") != 2 or len(packages) < 1:
        raise ValueError(
            "Preview download metadata must retain at least one explicit preview."
        )
    release_ids = set()
    retained_previews = set()
    for preview in packages:
        release_id = str(preview.get("releaseId", ""))
        retained_preview = (
            str(preview.get("platform", "")),
            str(preview.get("architecture", "")),
            str(preview.get("version", "")),
        )
        if (
            preview.get("type") != "hubInstallerPreview"
            or preview.get("platform") not in {"windows", "macos", "linux"}
            or preview.get("signed") is not False
            or preview.get("developmentArtifact") is not True
            or not str(preview.get("editorVersion", ""))
            or not str(preview.get("publishedAt", "")).endswith("Z")
            or not release_id
            or release_id in release_ids
            or retained_preview in retained_previews
            or not str(preview.get("url", "")).startswith("/preview-downloads/")
            or str(preview.get("sha256", ""))[:8] not in str(preview.get("fileName", ""))
            or len(str(preview.get("sha256", ""))) != 64
            or int(preview.get("sizeBytes", 0)) < 1
        ):
            raise ValueError(
                "Preview download metadata does not preserve its unique retained unsigned development identity."
            )
        release_ids.add(release_id)
        retained_previews.add(retained_preview)

    contact = (WEBSITE / "assets" / "contact.js").read_text(encoding="utf-8")
    if (
        "website-contact" not in contact
        or "textContent" not in contact
        or "innerHTML" in contact
    ):
        raise ValueError("Contact form endpoint or safe status rendering is missing.")

    caddy = (
        ROOT
        / "Services"
        / "KeireDistributionService"
        / "Deployment"
        / "Caddyfile.example"
    ).read_text(encoding="utf-8")
    for contract in (
        "not path /preview-downloads/*",
        "handle_path /preview-downloads/*",
        "KEIRE_PREVIEW_DOWNLOAD_ROOT",
        "khjduyjamzwumhducmou.supabase.co",
        "@docs_immutable path /docs/_astro/*",
        "'wasm-unsafe-eval'",
        "worker-src 'self' blob:",
    ):
        if contract not in caddy:
            raise ValueError(f"Caddy website contract is missing '{contract}'.")
    if caddy.count("import security_headers") != 2 or (
        "handle_errors {\n\t\timport security_headers" not in caddy
    ):
        raise ValueError(
            "Caddy normal and error routes must share the security-header policy."
        )

    print(f"Website validation passed for {len(pages)} HTML pages.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
