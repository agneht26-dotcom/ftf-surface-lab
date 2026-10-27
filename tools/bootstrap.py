"""Download development dependencies locally; never installs system-wide."""
from __future__ import annotations
import concurrent.futures
import hashlib
import json
import pathlib
import shutil
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPS = ROOT / "third_party"
TOOLS = ROOT / ".tools"
CACHE = ROOT / ".downloads"
HEADERS = {"User-Agent": "FTFSurfaceEditor-development"}

def get(url: str) -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=120) as response:
        return response.read()

def install(name: str, url: str, destination: pathlib.Path) -> dict:
    marker = destination / ".download.json"
    if marker.exists():
        print(f"Ready: {name}", flush=True)
        return json.loads(marker.read_text())
    archive = CACHE / (name + ".zip")
    if not archive.exists():
        print(f"Downloading: {name}", flush=True)
        archive.write_bytes(get(url))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    staging = CACHE / (name + "-unpack")
    staging.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            target = (staging / entry.filename).resolve()
            if not target.is_relative_to(staging.resolve()):
                raise RuntimeError("Unsafe archive member")
        package.extractall(staging)
    children = list(staging.iterdir())
    source = children[0] if len(children) == 1 and children[0].is_dir() else staging
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, dirs_exist_ok=True)
    record = {"name": name, "url": url, "sha256": digest}
    marker.write_text(json.dumps(record, indent=2), encoding="utf8")
    print(f"Installed locally: {name}", flush=True)
    return record

def main() -> None:
    CACHE.mkdir(exist_ok=True)
    jobs = [
        ("cmake-3.31.8", "https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip", TOOLS / "cmake"),
        ("glfw-3.4", "https://github.com/glfw/glfw/archive/refs/tags/3.4.zip", DEPS / "glfw"),
        ("imgui-1.91.9b", "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip", DEPS / "imgui"),
        ("vulkan-headers-1.3.290", "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.290.zip", DEPS / "vulkan-headers"),
        ("volk-1.3.290", "https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.3.290.0.zip", DEPS / "volk"),
        ("glm-1.0.1", "https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip", DEPS / "glm"),
        ("json-3.11.3", "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip", DEPS / "json"),
    ]
    shader_destination = TOOLS / "glslang"
    shader_marker = shader_destination / ".download.json"
    if shader_marker.exists():
        shader_record = json.loads(shader_marker.read_text())
        shader_url = shader_record["url"]
    else:
        release = json.loads(get("https://api.github.com/repos/KhronosGroup/glslang/releases/tags/main-tot"))
        assets = [x for x in release["assets"] if "windows" in x["name"].lower() and "release" in x["name"].lower() and x["name"].endswith(".zip")]
        if len(assets) != 1:
            raise RuntimeError(f"Choose shader compiler asset explicitly: {[x['name'] for x in release['assets']]}")
        shader_url = assets[0]["browser_download_url"]
    jobs.append(("glslang-development", shader_url, shader_destination))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        futures = [pool.submit(install, *job) for job in jobs]
        records = [future.result() for future in futures]
        print("Downloading reference-code recovery tool", flush=True)
    miniz=DEPS/'miniz';miniz.mkdir(parents=True,exist_ok=True)
    miniz_files=['miniz.c','miniz.h','miniz_common.h','miniz_tdef.c','miniz_tdef.h','miniz_tinfl.c','miniz_tinfl.h','miniz_zip.c','miniz_zip.h','LICENSE']
    hashes={}
    for filename in miniz_files:
        path=miniz/filename
        if not path.exists():path.write_bytes(get('https://raw.githubusercontent.com/richgel999/miniz/3.1.0/'+filename))
        hashes[filename]=hashlib.sha256(path.read_bytes()).hexdigest()
    (miniz/'miniz_export.h').write_text('#pragma once\n#define MINIZ_EXPORT\n')
    records.append({'name':'miniz-3.1.0','url':'https://github.com/richgel999/miniz/tree/3.1.0','files':hashes})
    (ROOT / "dependencies.lock.json").write_text(json.dumps(records, indent=2), encoding="utf8")

if __name__ == "__main__":
    main()
