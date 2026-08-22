import changelogSource from "../../../../../CHANGELOG.md?raw";
import projectConfiguration from "../../../../../Config/Project.conf?raw";
import { parseChangelog } from "./changelog-model.mjs";

export type ChangeAvailability = "released" | "live" | "source";

export interface ReleaseChange {
    text: string;
    availability: ChangeAvailability;
}

export interface ReleaseGroup {
    id: string;
    label: string;
    description: string;
    changes: ReleaseChange[];
}

export interface ReleaseNote {
    version: string;
    releaseDate: string | null;
    summary: string;
    highlights: ReleaseChange[];
    groups: ReleaseGroup[];
    evidence: {
        validation: readonly string[];
        limitations: readonly string[];
    };
    changeCount: number;
    current: boolean;
}

interface ParsedChangelogRelease {
    version: string;
    date: string | null;
    groups: Array<{ label: string; entries: string[] }>;
}

interface CategoryDefinition {
    id: string;
    label: string;
    description: string;
    patterns: RegExp[];
}

const categoryDefinitions: readonly CategoryDefinition[] = [
    {
        id: "audio",
        label: "Audio",
        description: "Mixing, routing, spatial playback, environmental audio, profiling, and managed audio control.",
        patterns: [/\baudio\b/i, /miniaudio/i, /sidechain/i, /reverb/i, /listener/i, /\bvoice/i, /mix console/i],
    },
    {
        id: "animation-gameplay",
        label: "Animation, IK, scripting, and gameplay",
        description: "Character solving, managed gameplay services, runtime handles, scripting, and play-mode behavior.",
        patterns: [/\bik\b/i, /ground adaptation/i, /animat/i, /coroutine/i, /rigid body/i, /managed gameplay/i,
            /managed scripting/i, /c# ide/i, /script assemblies/i],
    },
    {
        id: "rendering-graphs-vfx",
        label: "Rendering, graphs, and VFX",
        description: "Rendering backends, model import, Shader Graph, Material Graph, VFX, previews, and sample content.",
        patterns: [/shader/i, /material graph/i, /\bvfx\b/i, /render/i, /d3d12/i, /direct3d12/i, /vulkan/i,
            /model import/i, /mesh/i, /sandbox showcase/i, /material lab/i, /thumbnail/i],
    },
    {
        id: "marketplace-web",
        label: "Marketplace, identity, and web platform",
        description: "Accounts, publishing, validation, moderation, entitlements, downloads, and the Kéire web platform.",
        patterns: [/marketplace/i, /publisher/i, /supabase/i, /oauth/i, /website/i, /caddy/i, /github sign-in/i,
            /contact-field/i, /account/i, /storage/i, /content security policy/i, /validator/i, /entitlement/i],
    },
    {
        id: "platforms-packaging",
        label: "Platforms and packaging",
        description: "Windows and Linux packaging, supported host setup, toolchains, and cross-platform release work.",
        patterns: [/\blinux\b/i, /\bwindows\b/i, /\bmacos\b/i, /ubuntu/i, /rocky/i, /fedora/i, /\brpm\b/i,
            /\bdeb\b/i, /toolchain/i, /cross-platform packaging/i, /glibc/i],
    },
    {
        id: "hub-projects-distribution",
        label: "Hub, projects, and distribution",
        description: "Editor discovery, project publication, installation, updates, package integrity, and distribution services.",
        patterns: [/\bhub\b/i, /installer/i, /editor removals/i, /distribution/i, /download experience/i,
            /build support/i, /project publication/i, /project-package/i, /template-creation/i, /snapshot/i],
    },
    {
        id: "editor-authoring",
        label: "Editor and authoring",
        description: "Inspector, workspace, authoring, preview, external-tool, scene, and asset workflow improvements.",
        patterns: [/\beditor\b/i, /inspector/i, /external-editor/i, /scene camera/i, /workspace/i, /asset browser/i,
            /euler/i, /preview evaluation/i],
    },
    {
        id: "reliability-security",
        label: "Reliability and security",
        description: "Failure containment, integrity checks, recovery behavior, diagnostics, and trust boundaries.",
        patterns: [/hardened/i, /prevented/i, /fixed/i, /bounded/i, /cryptographic/i, /signature/i, /security/i,
            /recovery/i, /transactional/i, /malware/i, /firewall/i],
    },
    {
        id: "engine-runtime",
        label: "Engine and runtime",
        description: "Core runtime behavior, asset processing, player execution, and general engine capabilities.",
        patterns: [/.*/],
    },
];

const summaries: Readonly<Record<string, string>> = {
    "0.4.0": "A unified graph-authoring and Unity-shaped scripting milestone with schema-4 Shader/Material graphs, executable VFX subgraphs, and explicit release-candidate boundaries.",
    "0.3.2": "A cross-platform preview adding procedural humanoid locomotion, Linux Hub packages, and stronger release/runtime reliability.",
    "0.3.1": "A production-oriented preview centered on the asset ecosystem, modern authoring workflows, audio, animation, Hub reliability, and cross-platform release foundations.",
    "0.3.0": "The visual-authoring milestone that separated reusable Shader Graphs from Material Graphs and established the production Sandbox showcase.",
    "0.2.0": "A broad Editor and engine expansion covering asset workflows, rendering, diagnostics, authoring, and project compatibility.",
    "0.1.0": "The first packaged Kéire technology preview with the core engine, Editor, Hub, SDK, and deterministic project foundations.",
};

const parsedReleases = parseChangelog(changelogSource) as ParsedChangelogRelease[];
const currentVersion = /^PROJECT_VERSION=(.+)$/m.exec(projectConfiguration)?.[1]?.trim() ?? "0.4.0";

function flatten(release: ParsedChangelogRelease | undefined): string[] {
    return release?.groups.flatMap((group) => group.entries) ?? [];
}

function isLivePlatformChange(text: string): boolean {
    return /website|caddy|supabase|publisher|marketplace claims|marketplace catalog|staff operations|oauth sessions|github sign-in|content security policy|production-readiness review/i.test(text)
        && !/first-party website changelog|website-to-editor|hub strictly|editor package manager|windows hub sessions|hub installers/i.test(text);
}

function categoryFor(text: string): CategoryDefinition {
    return categoryDefinitions.find((category) => category.patterns.some((pattern) => pattern.test(text)))
        ?? categoryDefinitions[categoryDefinitions.length - 1];
}

function groupChanges(changes: ReleaseChange[]): ReleaseGroup[] {
    const grouped = new Map<string, ReleaseChange[]>();
    for (const change of changes) {
        const category = categoryFor(change.text);
        const existing = grouped.get(category.id) ?? [];
        existing.push(change);
        grouped.set(category.id, existing);
    }
    return categoryDefinitions.flatMap((category) => {
        const categoryChanges = grouped.get(category.id);
        return categoryChanges?.length ? [{ ...category, changes: categoryChanges }] : [];
    });
}

function pickHighlights(changes: ReleaseChange[], version: string): ReleaseChange[] {
    if (version !== currentVersion) {
        return changes.slice(0, Math.min(4, changes.length));
    }
    const preferred = [
        /explicit native Hub package-format identity/i,
        /zero-clip `ProceduralHumanoid`/i,
        /Play Mode crash when procedural state-change/i,
        /website-to-editor marketplace workflow/i,
        /full-body ik and ground adaptation/i,
        /audio workflow around a live channel-based mix console/i,
        /native standalone hub rpm packaging/i,
        /canonical and packaged sandbox around a clean material lab/i,
        /feature-gated 0\.3\.1 marketplace and asset-package foundation/i,
    ];
    const selected = preferred.flatMap((pattern) => changes.find((change) => pattern.test(change.text)) ?? []);
    return selected.length ? selected : changes.slice(0, Math.min(6, changes.length));
}

function buildReleaseNote(release: ParsedChangelogRelease): ReleaseNote {
    const baseChanges: ReleaseChange[] = flatten(release).map((text) => ({ text, availability: "released" }));
    const isCurrent = release.version === currentVersion;
    const sourceRelease = parsedReleases.find((candidate) => candidate.version === "Unreleased");
    const updates: ReleaseChange[] = isCurrent
        ? flatten(sourceRelease).map((text) => ({
            text,
            availability: isLivePlatformChange(text) ? "live" : "source",
        }))
        : [];
    const changes = [...updates, ...baseChanges];
    const currentEvidence = {
        validation: [
            "The canonical release record, website source contracts, generated routes, local links, structured metadata, RSS output, documentation search, and production CSP are validated during every site build.",
            "The public Windows package remains independently catalog-verified; exact native suite counts and rendered-output evidence are maintained in the production-readiness review.",
            "Marketplace artifacts retain quarantine, validator, moderation, signature, immutable publication, entitlement, and download evidence as separate trust decisions.",
        ],
        limitations: [
            `Kéire ${currentVersion} remains a pre-1.0 technology preview rather than a completed AAA production claim.`,
            "Windows and Linux packages are published for x86-64; macOS remains source-compatible but unadvertised pending Metal, signing, and notarization validation.",
            `Marketplace, publisher, community, and paid-checkout capabilities remain subject to their explicit feature flags and launch gates; paid checkout is disabled for ${currentVersion}.`,
        ],
    } as const;
    return {
        version: release.version,
        releaseDate: release.date,
        summary: summaries[release.version] ?? "A versioned Kéire Engine release with documented implementation and validation evidence.",
        highlights: pickHighlights(changes, release.version),
        groups: groupChanges(changes),
        evidence: isCurrent ? currentEvidence : {
            validation: ["Historical release notes record the evidence available when this version was current."],
            limitations: ["Historical availability does not imply current support, compatibility, or security maintenance."],
        },
        changeCount: changes.length,
        current: isCurrent,
    };
}

export const releaseNotes = parsedReleases
    .filter((release) => release.version !== "Unreleased")
    .map(buildReleaseNote);

export const currentRelease = releaseNotes.find((release) => release.current) ?? releaseNotes[0];

export function getReleaseNote(version: string): ReleaseNote | undefined {
    return releaseNotes.find((release) => release.version === version);
}

export function formatChangelogInline(value: string): string {
    const escape = (text: string) => text
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
    const tokens: string[] = [];
    const tokenized = value.replace(/`([^`]+)`|\*\*([^*]+)\*\*/g, (_match, code, strong) => {
        const html = code !== undefined ? `<code>${escape(code)}</code>` : `<strong>${escape(strong)}</strong>`;
        const token = `@@KEIRETOKEN${tokens.length}@@`;
        tokens.push(html);
        return token;
    });
    return tokens.reduce((html, token, index) => html.replace(`@@KEIRETOKEN${index}@@`, token), escape(tokenized));
}
