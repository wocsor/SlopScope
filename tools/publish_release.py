#!/usr/bin/env python3
"""Publish verified CI assets at an immutable release tag."""

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def github(*arguments):
    return subprocess.check_output(["gh", *arguments], text=True)


def github_optional(endpoint):
    result = subprocess.run(["gh", "api", endpoint], capture_output=True, text=True)
    try:
        body = json.loads(result.stdout)
    except ValueError:
        raise RuntimeError(result.stderr or "GitHub returned an invalid response") from None
    if result.returncode:
        if str(body.get("status")) == "404":
            return None
        raise RuntimeError(result.stderr or body.get("message", "GitHub request failed"))
    return body


def verified_assets(directory, commit, tag):
    checksums = {}
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        if Path(name).name != name or name in (".", "..") or name in checksums:
            raise ValueError("Invalid or duplicate asset name in SHA256SUMS")
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing or invalid release asset: {name}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Release checksum mismatch: {name}")
        checksums[name] = digest
    required = {
        "SlopScope-fel.bin", "Scope.update", "LICENSE", "THIRD_PARTY_NOTICES.md",
        "source-commit.txt", "update-manifest.json", "fel-install.md", "stock-updater.md",
    }
    if not required.issubset(checksums):
        raise ValueError("Release package is missing required files")
    if (directory / "source-commit.txt").read_text().strip() != commit:
        raise ValueError("Release assets were built from a different commit")
    manifest = json.loads((directory / "update-manifest.json").read_text())
    if manifest["destination_version"] != "V" + tag.removeprefix("v"):
        raise ValueError("Stock updater version does not match the release tag")
    if manifest["fel_image_sha256"] != checksums["SlopScope-fel.bin"]:
        raise ValueError("FEL manifest does not match the release image")
    if manifest["package_sha256"] != checksums["Scope.update"]:
        raise ValueError("Updater manifest does not match Scope.update")
    names = set(checksums) | {"SHA256SUMS"}
    if names != {path.name for path in directory.iterdir()}:
        raise ValueError("Release directory contains files outside SHA256SUMS")
    return [directory / name for name in sorted(names)]


def release_for_tag(prefix, tag):
    release = github_optional(f"{prefix}/releases/tags/{tag}")
    if release:
        return release
    page = 1
    while True:
        releases = json.loads(github("api", f"{prefix}/releases?per_page=100&page={page}"))
        for candidate in releases:
            if candidate["tag_name"] == tag:
                return candidate
        if len(releases) < 100:
            return None
        page += 1


def publish(repository, tag, commit, assets):
    prefix = f"repos/{repository}"
    existing = release_for_tag(prefix, tag)
    reference = github_optional(f"{prefix}/git/ref/tags/{tag}")
    target = json.loads(github("api", f"{prefix}/commits/{tag}")) if reference else None
    if target and target["sha"] != commit:
        raise ValueError("Release tag already points at another commit; use a new version")
    if existing:
        if existing["target_commitish"] != commit:
            raise ValueError("Existing release targets another commit; use a new version")
        if not existing["draft"]:
            if not target:
                raise ValueError("Published release has no resolvable tag")
            print(f"{tag} is already published for this commit; leaving its assets unchanged")
            return
    else:
        notes = (
            f"Built from `{commit}`.\n\n"
            "- `SlopScope-fel.bin`: flash locally at `0x349000` using FEL.\n"
            "- `Scope.update`: stock DOS1102 V4.0.0 USB-drive updater.\n\n"
            "Read the attached installation instructions and RELEASE_READINESS.md. "
            "Verify SHA256SUMS and retain LICENSE and THIRD_PARTY_NOTICES.md. "
            "The original pre-application logo is retained.\n"
        )
        arguments = [
            "release", "create", tag, "--repo", repository, "--target", commit,
            "--draft", "--title", f"SlopScope {tag}", "--notes", notes,
        ]
        if "-" in tag:
            arguments.append("--prerelease")
        github(*arguments)
    # Only an unpublished draft for this exact commit can have assets replaced.
    github("release", "upload", tag, "--repo", repository, "--clobber", *map(str, assets))
    release = release_for_tag(prefix, tag)
    if not release or not release["draft"] or release["target_commitish"] != commit:
        raise ValueError("Release is no longer a draft for this commit")
    uploaded = {asset["name"]: asset for asset in release["assets"]}
    if set(uploaded) != {path.name for path in assets}:
        raise ValueError("Draft contains unexpected assets; resolve them before publishing")
    for path in assets:
        asset = uploaded.get(path.name)
        if not asset or asset["size"] != path.stat().st_size:
            raise ValueError(f"Incomplete release upload: {path.name}")
        digest = asset.get("digest")
        if digest and digest != "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError(f"Uploaded release checksum mismatch: {path.name}")
    print(github("release", "edit", tag, "--repo", repository, "--draft=false"), end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--assets-dir", required=True, type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        parser.error("Expected an OWNER/REPOSITORY name")
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?", args.tag):
        parser.error("Expected a version tag such as v0.1.0 or v0.1.0-rc.1")
    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        parser.error("Expected the exact 40-character source commit")
    assets = verified_assets(args.assets_dir, args.commit, args.tag)
    if args.dry_run:
        print(json.dumps({"repository": args.repository, "tag": args.tag,
                          "commit": args.commit, "assets": [path.name for path in assets]}, indent=2))
        return
    publish(args.repository, args.tag, args.commit, assets)


if __name__ == "__main__":
    main()
