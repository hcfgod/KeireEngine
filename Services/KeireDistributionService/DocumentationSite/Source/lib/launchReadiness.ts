export interface LaunchReadinessTrack {
    id: string;
    status: string;
    title: string;
    completed: number;
    total: number;
    delivered: string;
    remaining: string;
}

export const launchReadinessTracks: readonly LaunchReadinessTrack[] = [
    {
        id: "web",
        status: "Substantial",
        title: "Website and documentation platform",
        completed: 9,
        total: 10,
        delivered: "Unified Astro website and documentation, shared responsive design, Caddy deployment, account surfaces, marketplace routes, local search, health checks, and transactional rollout.",
        remaining: "Complete the formal accessibility/browser matrix and repeat the performance-budget audit on the branded production domain.",
    },
    {
        id: "identity",
        status: "Active validation",
        title: "Identity and account security",
        completed: 8,
        total: 10,
        delivered: "Supabase SSR sessions, verified GitHub PKCE sign-in, MFA surfaces, hardened redirects, persistent account presentation, canonical-origin mutation protection, registered Hub OAuth, and packaged URL-protocol handoff support.",
        remaining: "Enable leaked-password protection, configure production SMTP, and finish native Windows/Linux revocation and recovery acceptance.",
    },
    {
        id: "packages",
        status: "Foundation complete",
        title: "Asset packages and Editor integration",
        completed: 11,
        total: 12,
        delivered: "Deterministic archives, bounded parsing, hashes and signature hooks, dependency resolution, lockfiles, read-only mounts, embedding, selective import, executable-code consent, and transactional recovery.",
        remaining: "Publish signed packages and pass complete install, update, remove, and player-build scenarios on Windows and Linux.",
    },
    {
        id: "marketplace",
        status: "Release candidate",
        title: "Marketplace and publishing workflows",
        completed: 10,
        total: 12,
        delivered: "Forced-RLS data model, private Storage boundaries, versioned APIs, official draft products, resumable uploads, isolated validation, publisher submission, staff moderation, automatic metadata-only signing, immutable publication, and free-entitlement contracts.",
        remaining: "Publish and claim the first official package through the complete live path, then approve the marketplace legal policies.",
    },
    {
        id: "operations",
        status: "Acceptance",
        title: "Validation and release operations",
        completed: 11,
        total: 12,
        delivered: "Atomic validator leases, scoped broker boundary, isolated worker, archive validation, malware and secret scanning, no-network C# compilation, real quarantine and moderation evidence, deterministic first-party package artifacts, dedicated Ed25519 trust roots, a durable publication queue, and a least-privileged metadata signer.",
        remaining: "Publish the official package set, rehearse restore and key rotation, and complete the native platform acceptance matrix as one release-operations gate.",
    },
] as const;

export const completedReadinessChecks = launchReadinessTracks.reduce((sum, track) => sum + track.completed, 0);
export const totalReadinessChecks = launchReadinessTracks.reduce((sum, track) => sum + track.total, 0);
