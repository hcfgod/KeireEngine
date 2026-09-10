import { copyFile, mkdir, rm, stat } from "node:fs/promises";
import path from "node:path";
import sharp from "sharp";

const stills = ["arrival", "materials", "vehicle", "effects", "editor", "graph"];
const films = ["outpost-tour"];

export async function syncShowcaseMedia(siteRoot) {
    const source = path.join(siteRoot, "Source", "media", "outpost");
    const destination = path.join(siteRoot, "public", "assets", "outpost");
    const files = [...stills.map(name => `${name}.webp`), ...films.map(name => `${name}.mp4`), "tour-description.vtt"];
    // Validate the entire set before replacing any previously generated media.
    for (const file of files) {
        const filePath = path.join(source, file);
        const details = await stat(filePath);
        const limit = file.endsWith(".webp") ? 600_000 : file === "outpost-tour.mp4" ? 18_000_000 : 5_000_000;
        if (!details.isFile() || details.size === 0 || details.size > limit) {
            throw new Error(`Showcase media is empty or exceeds its transfer budget: ${file}`);
        }
        if (file.endsWith(".webp")) {
            const metadata = await sharp(filePath).metadata();
            if (metadata.width !== 1920 || metadata.height !== 1080) {
                throw new Error(`Showcase still must be 1920 × 1080: ${file}`);
            }
        }
    }
    await mkdir(destination, { recursive: true });
    for (const file of files) await copyFile(path.join(source, file), path.join(destination, file));
    for (const name of ["materials-loop", "animation-loop", "effects-loop"]) {
        await rm(path.join(destination, `${name}.mp4`), { force: true });
    }
}
