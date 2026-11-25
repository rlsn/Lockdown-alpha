# Third-party asset sources (SourceData/ThirdParty/third_party_assets.json): checks the manifest, installs the local-only
# packs into this worktree and keeps restricted content out of Git.
#   python -B Scripts/ThirdPartyAssets.py check    manifest rules + no local-only content in the Git index, staged files or LFS
#   python -B Scripts/ThirdPartyAssets.py link     link Content/ThirdParty/Fab to $LD_THIRDPARTY_ROOT/Fab (a directory junction,
#                                                  so every worktree shares one copy) and report which local-only packs are present
# Exit code 1 on any problem; every problem is printed.
import json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "SourceData", "ThirdParty", "third_party_assets.json")
LOCAL = "Content/ThirdParty/Fab"
RIGHTS = ("acquisition_cost", "commercial_use", "ai_input", "ai_training", "public_source_redistribution", "private_collaborator_sharing")
DOMAIN_MANIFESTS = {"SourceData/Art/art_assets.json": "art", "SourceData/Audio/audio_cues.json": "audio"}


def load():
    with open(MANIFEST, encoding="utf-8") as handle:
        return json.load(handle)


def git(*args):
    out = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    return out.stdout.splitlines() if out.returncode == 0 else []


def check():
    doc, problems = load(), []
    ids = set()
    for asset in doc["assets"]:
        name = asset.get("id", "?")
        if name in ids:
            problems.append(f"{name}: duplicate id")
        ids.add(name)
        rights = asset.get("rights", {})
        for key in RIGHTS:
            if not rights.get(key):
                problems.append(f"{name}: rights.{key} missing")
        public = str(rights.get("public_source_redistribution", "")).startswith("yes")
        if asset.get("storage") not in ("tracked", "local-only"):
            problems.append(f"{name}: storage must be tracked or local-only")
        elif not public and asset["storage"] != "local-only":
            problems.append(f"{name}: no public redistribution, so storage must be local-only")
        for path in [asset.get("original_path")] + list(asset.get("adapted_paths") or []):
            if path and asset.get("storage") == "local-only" and not path.replace("\\", "/").startswith(LOCAL + "/"):
                problems.append(f"{name}: local-only content must live under {LOCAL}/: {path}")
    # Every non-project pack of the domain manifests is listed.
    for path, domain in DOMAIN_MANIFESTS.items():
        with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
            packs = json.load(handle).get("packs", [])
        for pack in packs:
            if str(pack.get("license", "")).startswith("Project-owned"):
                continue
            if pack["id"] not in ids:
                problems.append(f"{path} pack {pack['id']} ({domain}) is not in the third-party manifest")
    # Restricted content never enters Git: tracked, staged or in LFS.
    for listing, what in ((git("ls-files", LOCAL), "tracked"), (git("diff", "--cached", "--name-only", "--", LOCAL), "staged"),
                          ([line.split(" ", 2)[-1] for line in git("lfs", "ls-files")], "in Git LFS")):
        for path in listing:
            if path.replace("\\", "/").startswith(LOCAL + "/"):
                problems.append(f"restricted content {what}: {path}")
    if not any(line.strip() == "/" + LOCAL + "/" for line in open(os.path.join(ROOT, ".gitignore"), encoding="utf-8")):
        problems.append(f".gitignore does not ignore /{LOCAL}/")
    for problem in problems:
        print("PROBLEM", problem)
    print(f"third-party manifest: {len(doc['assets'])} assets, {len(problems)} problems")
    return not problems


def link():
    doc = load()
    store = os.environ.get("LD_THIRDPARTY_ROOT", "")
    target = os.path.join(ROOT, *LOCAL.split("/"))
    wanted = [a for a in doc["assets"] if a.get("storage") == "local-only" and a.get("original_path")]
    if not store:
        print("LD_THIRDPARTY_ROOT is not set: local-only packs cannot be installed." if wanted else "No local-only pack is installed yet.")
        return not wanted
    source = os.path.join(store, "Fab")
    os.makedirs(source, exist_ok=True)
    if not os.path.exists(target):
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if os.name == "nt":
            result = subprocess.run(["cmd", "/c", "mklink", "/J", target, source], capture_output=True, text=True)
        else:
            os.symlink(source, target)
            result = subprocess.CompletedProcess([], 0)
        if result.returncode != 0:
            print("PROBLEM could not link", target, "->", source, result.stdout, result.stderr)
            return False
        print("linked", target, "->", source)
    missing = [a["id"] for a in wanted if not os.path.isdir(os.path.join(ROOT, *a["original_path"].split("/")))]
    for name in missing:
        print("MISSING local-only pack", name)
    print(f"local-only packs: {len(wanted) - len(missing)} present, {len(missing)} missing")
    return not missing


if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else "check"
    sys.exit(0 if {"check": check, "link": link}[command]() else 1)
