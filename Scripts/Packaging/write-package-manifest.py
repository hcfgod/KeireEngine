#!/usr/bin/env python3
"""Write and validate deterministic Kéire product package manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path, PurePosixPath


SEMANTIC_VERSION = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class ManifestError(RuntimeError):
    pass


def parse_bool(value: str) -> bool:
    if value == "true":
        return True
    if value == "false":
        return False
    raise argparse.ArgumentTypeError("expected 'true' or 'false'")


def normalize_relative(value: str) -> str:
    normalized = value.replace("\\", "/")
    path = PurePosixPath(normalized)
    if (
        not normalized
        or path.is_absolute()
        or any(part in ("", ".", "..") for part in path.parts)
    ):
        raise ManifestError(f"package path is not confined: {value}")
    if ":" in path.parts[0]:
        raise ManifestError(f"package path contains a drive prefix: {value}")
    return path.as_posix()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def enumerate_files(stage: Path, exclusions: set[str]) -> list[dict[str, object]]:
    files: list[dict[str, object]] = []
    for root, directories, names in os.walk(stage, followlinks=False):
        root_path = Path(root)
        for directory in directories:
            candidate = root_path / directory
            if candidate.is_symlink():
                raise ManifestError(
                    f"package contains a symbolic-link directory: {candidate}"
                )
        for name in names:
            candidate = root_path / name
            if candidate.is_symlink() or not candidate.is_file():
                raise ManifestError(f"package contains a non-regular file: {candidate}")
            relative = candidate.relative_to(stage).as_posix()
            if relative in exclusions:
                continue
            files.append(
                {
                    "path": relative,
                    "sizeBytes": candidate.stat().st_size,
                    "sha256": sha256_file(candidate),
                }
            )
    files.sort(key=lambda item: str(item["path"]))
    return files


def discover_licenses(stage: Path) -> list[str]:
    licenses: list[str] = []
    for relative in (
        "LICENSE.txt",
        "THIRD_PARTY_NOTICES.md",
        "content/Fonts/Inter-OFL.txt",
        "content/Fonts/Material-Symbols-Apache-2.0.txt",
    ):
        if (stage / relative).is_file():
            licenses.append(relative)
    license_root = stage / "third-party" / "licenses"
    if license_root.is_dir():
        licenses.extend(
            path.relative_to(stage).as_posix()
            for path in license_root.rglob("*")
            if path.is_file() and not path.is_symlink()
        )
    return sorted(set(licenses))


def parse_entrypoint(value: str) -> tuple[str, str]:
    role, separator, path = value.partition("=")
    if not separator or not role or not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", role):
        raise ManifestError(f"invalid entrypoint declaration: {value}")
    return role, normalize_relative(path)


def parse_template(value: str) -> dict[str, str]:
    parts = value.split("|", 2)
    if len(parts) < 2 or not parts[0] or not SEMANTIC_VERSION.fullmatch(parts[1]):
        raise ManifestError(f"invalid template declaration: {value}")
    result = {"id": parts[0], "version": parts[1]}
    if len(parts) == 3 and parts[2]:
        result["path"] = normalize_relative(parts[2])
    return result


def parse_template_catalog(stage: Path, catalog_relative: str) -> list[dict[str, str]]:
    catalog_relative = normalize_relative(catalog_relative)
    catalog_path = require_stage_path(stage, catalog_relative, "template catalog")
    try:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ManifestError(
            f"template catalog is not valid UTF-8 JSON: {error}"
        ) from error
    if not isinstance(catalog, dict) or catalog.get("schemaVersion") != 1:
        raise ManifestError("template catalog schemaVersion must be 1")
    catalog_templates = catalog.get("templates")
    if not isinstance(catalog_templates, list) or not catalog_templates:
        raise ManifestError("template catalog must contain at least one template")

    catalog_root = PurePosixPath(catalog_relative).parent
    packaged_templates: list[dict[str, str]] = []
    for template in catalog_templates:
        if (
            not isinstance(template, dict)
            or not template.get("id")
            or not SEMANTIC_VERSION.fullmatch(str(template.get("version", "")))
        ):
            raise ManifestError("template catalog entry is invalid")
        payload_root = normalize_relative(str(template.get("payloadRoot", "")))
        package_payload_root = (catalog_root / payload_root).as_posix()
        payload_path = require_stage_path(
            stage,
            package_payload_root,
            f"template '{template['id']}' payload",
            directory_allowed=True,
        )
        payload_files = template.get("payloadFiles")
        if not isinstance(payload_files, list) or not payload_files:
            raise ManifestError(
                f"template '{template['id']}' has no declared payload files"
            )
        declared_files: set[str] = set()
        declared_size = 0
        for payload_file in payload_files:
            if not isinstance(payload_file, dict):
                raise ManifestError(
                    f"template '{template['id']}' payload entry is invalid"
                )
            payload_relative = normalize_relative(str(payload_file.get("path", "")))
            if payload_relative in declared_files:
                raise ManifestError(
                    f"template '{template['id']}' declares a duplicate payload file"
                )
            declared_files.add(payload_relative)
            payload = require_stage_path(
                stage,
                (PurePosixPath(package_payload_root) / payload_relative).as_posix(),
                f"template '{template['id']}' payload file",
            )
            if payload_file.get(
                "sizeBytes"
            ) != payload.stat().st_size or payload_file.get("sha256") != sha256_file(
                payload
            ):
                raise ManifestError(
                    f"template '{template['id']}' payload digest does not match: {payload_relative}"
                )
            declared_size += payload.stat().st_size
        if template.get("estimatedSizeBytes") != declared_size:
            raise ManifestError(
                f"template '{template['id']}' estimated size does not match its payload"
            )
        actual_files = {
            path.relative_to(payload_path).as_posix()
            for path in payload_path.rglob("*")
            if path.is_file() and not path.is_symlink()
        }
        if actual_files != declared_files:
            raise ManifestError(
                f"template '{template['id']}' payload contains undeclared or missing files"
            )
        thumbnail = template.get("thumbnail")
        if thumbnail:
            require_stage_path(
                stage,
                (catalog_root / normalize_relative(str(thumbnail))).as_posix(),
                f"template '{template['id']}' thumbnail",
            )
        packaged_templates.append(
            {
                "id": str(template["id"]),
                "version": str(template["version"]),
                "path": package_payload_root,
            }
        )
    return sorted(packaged_templates, key=lambda item: item["id"])


def validate_content_catalog(stage: Path, catalog_relative: str) -> None:
    catalog_path = require_stage_path(stage, catalog_relative, "content catalog")
    try:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ManifestError(
            f"content catalog is not valid UTF-8 JSON: {error}"
        ) from error
    if not isinstance(catalog, dict) or catalog.get("schemaVersion") != 1:
        raise ManifestError("content catalog schemaVersion must be 1")
    if not isinstance(catalog.get("locale"), str) or not catalog["locale"]:
        raise ManifestError("content catalog locale is invalid")

    identities: set[str] = set()
    for collection_name in ("learn", "resources"):
        collection = catalog.get(collection_name)
        if not isinstance(collection, list):
            raise ManifestError(f"content catalog {collection_name} must be an array")
        for item in collection:
            if (
                not isinstance(item, dict)
                or not isinstance(item.get("id"), str)
                or not item["id"]
            ):
                raise ManifestError(
                    f"content catalog {collection_name} entry is invalid"
                )
            if item["id"] in identities:
                raise ManifestError(
                    f"content catalog contains duplicate ID: {item['id']}"
                )
            identities.add(item["id"])
            local_path = item.get("localPath")
            url = item.get("url")
            if bool(local_path) == bool(url):
                raise ManifestError(
                    f"content '{item['id']}' must declare exactly one localPath or URL"
                )
            if local_path:
                require_stage_path(
                    stage, str(local_path), f"content '{item['id']}' target"
                )
            elif not isinstance(url, str) or not url.startswith("https://"):
                raise ManifestError(f"content '{item['id']}' URL must use HTTPS")


def validate_license_catalog(stage: Path, catalog_relative: str) -> None:
    catalog_path = require_stage_path(stage, catalog_relative, "license catalog")
    try:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ManifestError(
            f"license catalog is not valid UTF-8 JSON: {error}"
        ) from error
    if not isinstance(catalog, dict) or catalog.get("schemaVersion") != 1:
        raise ManifestError("license catalog schemaVersion must be 1")
    licenses = catalog.get("licenses")
    if not isinstance(licenses, list) or not licenses:
        raise ManifestError("license catalog must contain at least one license")
    identities: set[str] = set()
    for license_entry in licenses:
        if (
            not isinstance(license_entry, dict)
            or not isinstance(license_entry.get("id"), str)
            or not license_entry["id"]
        ):
            raise ManifestError("license catalog entry is invalid")
        if license_entry["id"] in identities:
            raise ManifestError(
                f"license catalog contains duplicate ID: {license_entry['id']}"
            )
        identities.add(license_entry["id"])
        require_stage_path(
            stage,
            str(license_entry.get("sourcePath", "")),
            f"license '{license_entry['id']}'",
        )


def parse_toolchain(value: str) -> dict[str, object]:
    parts = value.split("|", 2)
    if len(parts) != 3 or not all(parts):
        raise ManifestError(f"invalid toolchain declaration: {value}")
    return {
        "id": parts[0],
        "version": parts[1],
        "path": normalize_relative(parts[2]),
        "bundled": True,
        "readOnly": True,
    }


def serialized_manifest(manifest: dict[str, object]) -> bytes:
    return (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def manifest_fingerprint(manifest: dict[str, object]) -> str:
    fingerprint_payload = {
        key: value
        for key, value in manifest.items()
        if key not in ("manifestFingerprint", "installedSizeBytes")
    }
    canonical = json.dumps(
        fingerprint_payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def require_stage_path(
    stage: Path, relative: str, description: str, directory_allowed: bool = False
) -> Path:
    normalized = normalize_relative(relative)
    path = stage
    for component in PurePosixPath(normalized).parts:
        try:
            exact = next(
                (entry for entry in path.iterdir() if entry.name == component), None
            )
        except OSError as error:
            raise ManifestError(
                f"{description} could not be inspected in the package: {relative}"
            ) from error
        if exact is None:
            raise ManifestError(
                f"{description} does not exist with exact path casing in the package: {relative}"
            )
        if exact.is_symlink():
            raise ManifestError(
                f"{description} traverses a symbolic link in the package: {relative}"
            )
        path = exact
    valid = path.is_dir() if directory_allowed else path.is_file()
    if not valid:
        raise ManifestError(f"{description} does not exist in the package: {relative}")
    return path


def write_manifest(arguments: argparse.Namespace) -> None:
    stage = Path(arguments.stage).resolve(strict=True)
    if not stage.is_dir():
        raise ManifestError(f"package stage is not a directory: {stage}")
    output_relative = normalize_relative(arguments.output)
    output = stage / output_relative
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)

    entrypoints = dict(
        sorted(parse_entrypoint(value) for value in arguments.entrypoint)
    )
    if not entrypoints:
        raise ManifestError("at least one package entrypoint is required")
    for role, relative in entrypoints.items():
        require_stage_path(stage, relative, f"entrypoint '{role}'")

    templates = [parse_template(value) for value in arguments.template]
    template_catalog = (
        normalize_relative(arguments.template_catalog)
        if arguments.template_catalog
        else None
    )
    if template_catalog:
        templates.extend(parse_template_catalog(stage, template_catalog))
    templates.sort(key=lambda item: item["id"])
    for template in templates:
        if "path" in template:
            require_stage_path(
                stage, template["path"], f"template '{template['id']}'", True
            )

    toolchains = sorted(
        (parse_toolchain(value) for value in arguments.toolchain),
        key=lambda item: item["id"],
    )
    for toolchain in toolchains:
        require_stage_path(
            stage, str(toolchain["path"]), f"toolchain '{toolchain['id']}'", True
        )

    module_definition = normalize_relative(arguments.module_definition)
    module_path = require_stage_path(
        stage, module_definition, "source-module definition"
    )
    release_notes = normalize_relative(arguments.release_notes)
    require_stage_path(stage, release_notes, "release notes")
    if arguments.build_manifest:
        require_stage_path(stage, arguments.build_manifest, "build manifest")
    if arguments.artifact == "hub":
        validate_content_catalog(stage, "content/Content/en-US.json")
        validate_license_catalog(stage, "content/Licenses/catalog.json")

    inventory_exclusions = {output_relative}
    inventory = enumerate_files(stage, inventory_exclusions)
    legacy_fields = [
        "artifact",
        "project",
        "version",
        "commit",
        "dirty",
        "developmentArtifact",
        "platform",
        "architecture",
        "configuration",
        "launcher",
    ]
    manifest: dict[str, object] = {
        "schemaVersion": 2,
        "artifact": arguments.artifact,
        "packageId": f"{arguments.package_prefix}.{arguments.artifact}",
        "project": arguments.project,
        "version": arguments.version,
        "channel": arguments.channel,
        "commit": arguments.commit,
        "dirty": arguments.dirty,
        "developmentArtifact": arguments.development_artifact,
        "platform": arguments.platform,
        "architecture": arguments.architecture,
        "configuration": arguments.configuration,
        "launcher": normalize_relative(arguments.launcher),
        "compatibility": {
            "legacySchemaVersion": 1,
            "legacyTopLevelFields": legacy_fields,
        },
        "entrypoints": entrypoints,
        "projectSchema": {
            "minimum": arguments.project_schema_minimum,
            "maximum": arguments.project_schema_maximum,
        },
        "moduleDefinition": module_definition,
        "moduleFingerprint": sha256_file(module_path),
        "packagedTemplates": templates,
        "bundledToolchains": toolchains,
        "licenseReferences": discover_licenses(stage),
        "releaseNotes": release_notes,
        "inventoryExcludes": [output_relative],
        "files": inventory,
        "installedSizeBytes": sum(int(item["sizeBytes"]) for item in inventory),
    }
    if arguments.bundled_dotnet_sdk:
        manifest["bundledDotnetSdk"] = arguments.bundled_dotnet_sdk
        legacy_fields.append("bundledDotnetSdk")
    if arguments.build_manifest:
        manifest["buildManifest"] = normalize_relative(arguments.build_manifest)
        legacy_fields.append("buildManifest")
    if template_catalog:
        manifest["templateCatalog"] = template_catalog
    manifest["manifestFingerprint"] = manifest_fingerprint(manifest)

    # Include the manifest itself in installedSizeBytes without creating a self-hash cycle.
    for _ in range(8):
        data = serialized_manifest(manifest)
        installed_size = sum(int(item["sizeBytes"]) for item in inventory) + len(data)
        if manifest["installedSizeBytes"] == installed_size:
            break
        manifest["installedSizeBytes"] = installed_size
    else:
        raise ManifestError("package manifest size did not converge")

    output.write_bytes(serialized_manifest(manifest))
    validate_manifest(stage, output_relative, arguments.artifact)


def validate_manifest(
    stage: Path, manifest_relative: str, expected_artifact: str | None
) -> None:
    manifest_relative = normalize_relative(manifest_relative)
    manifest_path = require_stage_path(stage, manifest_relative, "package manifest")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ManifestError(
            f"package manifest is not valid UTF-8 JSON: {error}"
        ) from error

    required = (
        "schemaVersion",
        "artifact",
        "packageId",
        "project",
        "version",
        "channel",
        "commit",
        "dirty",
        "developmentArtifact",
        "platform",
        "architecture",
        "configuration",
        "launcher",
        "compatibility",
        "entrypoints",
        "projectSchema",
        "moduleDefinition",
        "moduleFingerprint",
        "manifestFingerprint",
        "packagedTemplates",
        "bundledToolchains",
        "licenseReferences",
        "releaseNotes",
        "inventoryExcludes",
        "files",
        "installedSizeBytes",
    )
    missing = [field for field in required if field not in manifest]
    if missing:
        raise ManifestError(f"package manifest is missing fields: {', '.join(missing)}")
    if manifest["schemaVersion"] != 2:
        raise ManifestError("package manifest schemaVersion must be 2")
    if expected_artifact and manifest["artifact"] != expected_artifact:
        raise ManifestError(f"package artifact must be '{expected_artifact}'")
    if not SEMANTIC_VERSION.fullmatch(str(manifest["version"])):
        raise ManifestError("package version is not semantic")
    if not isinstance(manifest["dirty"], bool) or not isinstance(
        manifest["developmentArtifact"], bool
    ):
        raise ManifestError("package worktree flags must be booleans")

    compatibility = manifest["compatibility"]
    if (
        not isinstance(compatibility, dict)
        or compatibility.get("legacySchemaVersion") != 1
    ):
        raise ManifestError("package manifest does not declare schema-1 compatibility")
    legacy_fields = compatibility.get("legacyTopLevelFields")
    if not isinstance(legacy_fields, list) or any(
        field not in manifest for field in legacy_fields
    ):
        raise ManifestError(
            "package manifest legacy top-level field contract is invalid"
        )

    schema_range = manifest["projectSchema"]
    if not isinstance(schema_range, dict):
        raise ManifestError("projectSchemaRange must be an object")
    minimum = schema_range.get("minimum")
    maximum = schema_range.get("maximum")
    if (
        not isinstance(minimum, int)
        or not isinstance(maximum, int)
        or minimum < 1
        or minimum > maximum
    ):
        raise ManifestError("project schema range is invalid")

    entrypoints = manifest["entrypoints"]
    if not isinstance(entrypoints, dict) or not entrypoints:
        raise ManifestError("package entrypoints must be a non-empty object")
    for role, relative in entrypoints.items():
        require_stage_path(stage, str(relative), f"entrypoint '{role}'")
    require_stage_path(stage, str(manifest["launcher"]), "legacy launcher")

    module_definition = str(manifest["moduleDefinition"])
    module_path = require_stage_path(
        stage, module_definition, "source-module definition"
    )
    module_fingerprint = str(manifest["moduleFingerprint"])
    if not SHA256.fullmatch(module_fingerprint) or module_fingerprint != sha256_file(
        module_path
    ):
        raise ManifestError(
            "source-module fingerprint does not match the packaged definition"
        )
    stored_manifest_fingerprint = str(manifest["manifestFingerprint"])
    if not SHA256.fullmatch(
        stored_manifest_fingerprint
    ) or stored_manifest_fingerprint != manifest_fingerprint(manifest):
        raise ManifestError(
            "package manifest fingerprint does not match its canonical metadata"
        )

    templates = manifest["packagedTemplates"]
    if not isinstance(templates, list):
        raise ManifestError("templates must be an array")
    template_ids: set[str] = set()
    for template in templates:
        if (
            not isinstance(template, dict)
            or not template.get("id")
            or not SEMANTIC_VERSION.fullmatch(str(template.get("version", "")))
        ):
            raise ManifestError("package template entry is invalid")
        if str(template["id"]) in template_ids:
            raise ManifestError(f"duplicate template ID: {template['id']}")
        template_ids.add(str(template["id"]))
        if "path" in template:
            require_stage_path(
                stage, str(template["path"]), f"template '{template['id']}'", True
            )
    if "templateCatalog" in manifest:
        catalog_templates = parse_template_catalog(
            stage, str(manifest["templateCatalog"])
        )
        catalog_identity = {
            (template["id"], template["version"], template["path"])
            for template in catalog_templates
        }
        packaged_identity = {
            (template.get("id"), template.get("version"), template.get("path"))
            for template in templates
        }
        if not catalog_identity.issubset(packaged_identity):
            raise ManifestError(
                "packaged template inventory does not match the template catalog"
            )
    if manifest["artifact"] == "hub":
        validate_content_catalog(stage, "content/Content/en-US.json")
        validate_license_catalog(stage, "content/Licenses/catalog.json")

    toolchains = manifest["bundledToolchains"]
    if not isinstance(toolchains, list):
        raise ManifestError("toolchains must be an array")
    for toolchain in toolchains:
        if (
            not isinstance(toolchain, dict)
            or not toolchain.get("id")
            or not toolchain.get("version")
        ):
            raise ManifestError("package toolchain entry is invalid")
        require_stage_path(
            stage,
            str(toolchain.get("path", "")),
            f"toolchain '{toolchain['id']}'",
            True,
        )

    release_notes = str(manifest["releaseNotes"])
    require_stage_path(stage, release_notes, "release notes")
    expected_licenses = discover_licenses(stage)
    if manifest["licenseReferences"] != expected_licenses:
        raise ManifestError(
            "package license inventory does not match packaged license files"
        )

    exclusions = manifest["inventoryExcludes"]
    if exclusions != [manifest_relative]:
        raise ManifestError("package inventory may exclude only its own manifest")
    expected_inventory = enumerate_files(stage, {manifest_relative})
    if manifest["files"] != expected_inventory:
        raise ManifestError("package file inventory does not match staged bytes")
    total_size = sum(path.stat().st_size for path in stage.rglob("*") if path.is_file())
    if manifest["installedSizeBytes"] != total_size:
        raise ManifestError("package installed size does not match staged bytes")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    write = subparsers.add_parser("write", help="write and validate a package manifest")
    write.add_argument("--stage", required=True)
    write.add_argument("--output", required=True)
    write.add_argument("--artifact", choices=("editor", "hub"), required=True)
    write.add_argument("--package-prefix", required=True)
    write.add_argument("--project", required=True)
    write.add_argument("--version", required=True)
    write.add_argument("--channel", default="Stable")
    write.add_argument("--commit", required=True)
    write.add_argument("--dirty", type=parse_bool, required=True)
    write.add_argument("--development-artifact", type=parse_bool, required=True)
    write.add_argument(
        "--platform", choices=("Windows", "Linux", "macOS"), required=True
    )
    write.add_argument("--architecture", choices=("x86_64", "ARM64"), required=True)
    write.add_argument("--configuration", default="Dist")
    write.add_argument("--launcher", required=True)
    write.add_argument("--build-manifest")
    write.add_argument("--bundled-dotnet-sdk")
    write.add_argument("--module-definition", required=True)
    write.add_argument("--project-schema-minimum", type=int, default=1)
    write.add_argument("--project-schema-maximum", type=int, default=3)
    write.add_argument("--entrypoint", action="append", default=[])
    write.add_argument("--template", action="append", default=[])
    write.add_argument("--template-catalog")
    write.add_argument("--toolchain", action="append", default=[])
    write.add_argument("--release-notes", default="CHANGELOG.md")

    validate = subparsers.add_parser(
        "validate", help="validate staged package bytes against a manifest"
    )
    validate.add_argument("--stage", required=True)
    validate.add_argument("--manifest", required=True)
    validate.add_argument("--artifact", choices=("editor", "hub"))
    return parser


def main() -> int:
    parser = create_parser()
    arguments = parser.parse_args()
    try:
        if arguments.command == "write":
            write_manifest(arguments)
        else:
            validate_manifest(
                Path(arguments.stage).resolve(strict=True),
                arguments.manifest,
                arguments.artifact,
            )
    except (ManifestError, OSError) as error:
        print(f"package manifest error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
