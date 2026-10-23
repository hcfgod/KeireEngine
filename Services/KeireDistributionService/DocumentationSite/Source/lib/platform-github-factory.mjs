import { loadWindowsGitHubSecrets } from "./platform-github-secrets.mjs";
import { createLocalGitHubService } from "./platform-github-http.mjs";

export function windowsGitHubServiceFactory(options) {
    const configuration = { ...options };
    return async ({ authentication }) => {
        const settings = await loadWindowsGitHubSecrets(configuration);
        try {
            return createLocalGitHubService({ authentication, settings });
        } finally {
            settings.clientSecret.fill(0);
        }
    };
}
