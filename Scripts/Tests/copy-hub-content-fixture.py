"""Copy authored Hub inputs without caches from locally opened template projects."""
import pathlib
import shutil
import sys


def ignore_generated(directory, names):
    if pathlib.Path(directory).parent.name == "Payloads":
        return set(names) & {"Library", "Temp", "Logs", "Build"}
    return set()


shutil.copytree(sys.argv[1], sys.argv[2], dirs_exist_ok=True, ignore=ignore_generated)
