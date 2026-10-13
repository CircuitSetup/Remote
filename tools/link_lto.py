Import("env")

env.Append(LINKFLAGS=["-flto", "-flto-partition=one"])
from pathlib import Path
import sys
sys.path.insert(0,str(Path(env.subst("$PROJECT_DIR"))/"tools"))
from embed_remote_ui import embed
embed(env.subst("$PROJECT_DIR"))
from embed_click_audio import generate
root=Path(env.subst("$PROJECT_DIR"))
generate(root/"src/remote_click.h",root/"src/remote_click_compressed.h")
