export function runtimeEnvironment(name: string): string | undefined {
    const value = process.env[name]?.trim();
    return value ? value : undefined;
}
