"""Pre-build script: regenerate radar_page.h from radar_page.html."""
import subprocess
import sys
import os

Import("env")

def generate_html_header(source, target, env):
    script = os.path.join(env.get("PROJECT_DIR"), "src", "embed_html.py")
    subprocess.check_call([sys.executable, script])

env.AddPreAction("buildprog", generate_html_header)
