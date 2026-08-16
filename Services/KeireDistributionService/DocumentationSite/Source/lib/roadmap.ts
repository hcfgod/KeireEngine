export type RoadmapHorizonId = "now" | "next" | "later";
export type RoadmapStatus = "Available" | "In validation" | "In development" | "Planned";

export interface RoadmapInitiative {
    title: string;
    outcome: string;
    status: RoadmapStatus;
    capabilities: readonly string[];
    evidenceUrl?: string;
}

export interface RoadmapHorizon {
    id: RoadmapHorizonId;
    label: string;
    timeframe: string;
    statement: string;
    initiatives: readonly RoadmapInitiative[];
}

export const roadmapHorizons: readonly RoadmapHorizon[] = [
    {
        id: "now",
        label: "Now",
        timeframe: "0.3.2 release train",
        statement: "Turn the current Windows technology preview into a dependable daily-development foundation.",
        initiatives: [
            {
                title: "Windows release reliability",
                outcome: "A trustworthy Hub-to-Editor installation, upgrade, project, and player-build path.",
                status: "Available",
                capabilities: [
                    "Catalog-verified Hub and Editor packages",
                    "Safe project upgrades and transactional publication",
                    "Direct3D 12 and Vulkan validation",
                    "Actionable launch, installation, and recovery diagnostics",
                ],
                evidenceUrl: "/changelog/0.3.2/",
            },
            {
                title: "Marketplace and package workflows",
                outcome: "Signed content can move from publisher quarantine to a verified project import without losing provenance.",
                status: "In validation",
                capabilities: [
                    "Publisher validation, moderation, and offline signing",
                    "Personal and organization entitlements",
                    "Hub download and signature verification",
                    "Editor Package Manager installation and recovery",
                ],
                evidenceUrl: "/docs/reference/marketplace-launch/",
            },
            {
                title: "Production authoring foundations",
                outcome: "Artists and gameplay teams can build, preview, diagnose, and reuse content without editing generated code.",
                status: "In development",
                capabilities: [
                    "Shader Graph and Material Graph interoperability",
                    "VFX authoring, diagnostics, and sample effects",
                    "Animation, automatic IK, and ground adaptation",
                    "Audio mixing, routing, spatial playback, and profiling",
                ],
                evidenceUrl: "/docs/reference/production-readiness-review/",
            },
        ],
    },
    {
        id: "next",
        label: "Next",
        timeframe: "Production workflows",
        statement: "Close the evidence and capability gaps that prevent representative teams from shipping confidently.",
        initiatives: [
            {
                title: "Supported native Linux releases",
                outcome: "Reproducible Hub, Editor, package, and player workflows on declared Ubuntu and Rocky/Fedora baselines.",
                status: "In validation",
                capabilities: [
                    "Clean DEB and RPM release artifacts",
                    "Desktop protocol and secure-session integration",
                    "Vulkan rendered-output acceptance",
                    "Install, update, remove, and Sandbox validation",
                ],
                evidenceUrl: "/docs/reference/production-readiness-review/",
            },
            {
                title: "Material and VFX capability closure",
                outcome: "Prioritize missing graph capabilities by production scenario rather than an untracked feature count.",
                status: "In development",
                capabilities: [
                    "Broader and consistently organized node libraries",
                    "Reusable functions, parameters, events, and outputs",
                    "Deterministic upgrades and recoverable failures",
                    "Cross-platform render and simulation coverage",
                ],
                evidenceUrl: "/docs/reference/material-parity-matrix/",
            },
            {
                title: "Performance and recovery evidence",
                outcome: "Make frame cost, memory pressure, long-session stability, and failure recovery measurable release contracts.",
                status: "Planned",
                capabilities: [
                    "Renderer timestamps and named hardware tiers",
                    "CPU, GPU, memory, and load-time budgets",
                    "Scene, VFX, audio, asset, and managed-reload soak tests",
                    "Retained artifacts and intentional baseline review",
                ],
                evidenceUrl: "/docs/reference/production-readiness-review/",
            },
        ],
    },
    {
        id: "later",
        label: "Later",
        timeframe: "Path to 1.0",
        statement: "Expand platform reach and team-scale workflows only after the current reliability boundaries are proven.",
        initiatives: [
            {
                title: "Native macOS and Metal support",
                outcome: "Source compatibility becomes an advertised platform only after native hardware validation and release signing.",
                status: "Planned",
                capabilities: [
                    "ARM64 and x86-64 native builds",
                    "Metal rendered-output validation",
                    "Hardened runtime, notarization, and stapling",
                    "Keychain and URL-activation acceptance",
                ],
            },
            {
                title: "Large-team production workflows",
                outcome: "Projects remain understandable and recoverable across bigger teams, depots, platforms, and content volumes.",
                status: "Planned",
                capabilities: [
                    "Source-control and large-depot workflows",
                    "Accessibility and localization programs",
                    "Content-team usability and upgrade rehearsals",
                    "Representative shipped-project support evidence",
                ],
            },
            {
                title: "A durable commercial ecosystem",
                outcome: "Marketplace and extension capabilities grow behind explicit compatibility, security, legal, and operational boundaries.",
                status: "Planned",
                capabilities: [
                    "Stable signed native plugin ABI",
                    "Expanded organization and publisher operations",
                    "Commercial checkout only after policy and tax readiness",
                    "Documented support and security response programs",
                ],
            },
        ],
    },
];
