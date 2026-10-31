from gzip import GzipFile
from pathlib import Path
from shutil import copyfileobj

Import("env")

if not env.IsIntegrationDump():
    project_dir = Path(env.subst("$PROJECT_DIR"))
    source = project_dir.parent / "web-remote" / "dist" / "device" / "index.html"
    destination = project_dir / "data" / "web" / "index.html.gz"

    if not source.is_file():
        print(f"Warning: web remote build not found at {source}; skipping gzip asset")
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + ".tmp")
        try:
            with source.open("rb") as html, temporary.open("wb") as output:
                with GzipFile(filename="", mode="wb", fileobj=output, compresslevel=9, mtime=0) as compressed:
                    copyfileobj(html, compressed)
            temporary.replace(destination)
            print(f"Compressed web remote: {source} -> {destination}")
        finally:
            temporary.unlink(missing_ok=True)
