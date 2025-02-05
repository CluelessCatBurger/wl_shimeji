import os
from posix import waitpid
import sys
import json
import socket
import argparse
import subprocess
from typing import Generator
import struct
import time
import tempfile
import shutil

from compiler import *
from qoi.src.qoi import encode_img

WL_SHIMEJI_FORMAT_VERSION_0_0_1 = (0,0,1)
WL_SHIMEJI_FORMAT_VERSION = (0,0,1)

from PIL import Image

import zipfile

# Shimeji VM compiler -----------

class ShimejiCtl:
    def __init__(self):
        self.parser = argparse.ArgumentParser(description='Control utility for wl_shimeji overlay')
        # self.parser.add_argument('action', help='Action to perform', choices=['summon', 'dismiss', 'stop', 'load-config', 'info', 'foreground', 'list', "convert"])
        self.parser.add_argument('--config-path', help='Path to the configurations directory', default=f'{os.environ.get("HOME", "/root")}/.local/share/wl_shimeji')

        # Subparsers
        self.subparsers = self.parser.add_subparsers(dest='action')
        self.summon_parser = self.subparsers.add_parser('summon', help='Summon a shimeji')
        self.summon_parser.add_argument('shimeji', help='Shimeji name')
        self.summon_parser.add_argument('--select', help='Select a specific spot', action='store_true')

        self.dismiss_parser = self.subparsers.add_parser('dismiss', help='Dismiss a shimeji')
        self.dismiss_parser.add_argument('--all', help='Dismiss all shimejis', action='store_true')
        self.dismiss_parser.add_argument('--all-other', help='Dismiss all shimejis except the specified one', action='store_true')
        self.dismiss_parser.add_argument('--same', help='Select only shimejis of same type', action='store_true')

        self.stop_parser = self.subparsers.add_parser('stop', help='Stop a shimeji')

        self.load_config_parser = self.subparsers.add_parser('load-config', help='Load a configuration')
        self.load_config_parser.add_argument('config', help='Shimeji config file path')

        self.info_parser = self.subparsers.add_parser('info', help='Get information about a shimeji')

        self.foreground_parser = self.subparsers.add_parser('foreground', help='Launch wl_shimeji in the foreground')

        self.list_parser = self.subparsers.add_parser('list', help='List all shimejis')

        self.converter_parser = self.subparsers.add_parser("convert", help="Convert Shimeji-ee mascot xml format to wl_shimeji mascot json format")

        self.converter_parser.add_argument("root_dir", type=str, help="Root directory of the mascot")
        self.converter_parser.add_argument("--output", "-o", type=str, help="Output directory")
        self.converter_parser.add_argument("--indent", "-i", type=int, default=4, help="Indentation level for the output JSON files")
        self.converter_parser.add_argument("--assets_dir", "-a", type=str, help="Assets directory", default="assets")
        self.converter_parser.add_argument("--name", type=str, help="Name of the mascot")
        self.converter_parser.add_argument("--author", type=str, help="Author of the mascot")
        self.converter_parser.add_argument("--version", type=str, help="Version of the mascot", default="0.0.1")
        self.converter_parser.add_argument("--description", type=str, help="Description of the mascot")
        self.converter_parser.add_argument("--display_name", '-n', type=str, help="Display name of the mascot")
        self.converter_parser.add_argument("--custom_actions_path", type=str, help="Path to custom actions file")
        self.converter_parser.add_argument("--custom_behaviors_path", type=str, help="Path to custom behaviors file")
        self.converter_parser.add_argument("--custom_programs_path", type=str, help="Path to custom programs file")
        self.converter_parser.add_argument("-f", "--force", help="Overwrite mascot", action='store_true')

        self.importer_parser = self.subparsers.add_parser("import", help="Import shimeji pack to wl_shimeji. Supports both Shimeji-ee instance and wl_shimeji pack formats")
        self.importer_parser.add_argument("path", type=str, help="path to root directory of the Shimeji-EE instance or zip archive")
        self.importer_parser.add_argument("--shimeji-ee", "-e", action="store_true", help="Treat path as Shimeji-EE intance")
        self.importer_parser.add_argument("--wl_shimeji", "-w", action="store_true", help="Treat path as wl_shimeji pack")
        self.importer_parser.add_argument("--force", "-f", action="store_true", help="Overwrite existing shimejis with same name")

        self.exporter_parser = self.subparsers.add_parser("export", help="Export wl_shimeji pack to wl_shimeji pack format")
        self.exporter_parser.add_argument("shimejis", type=str, nargs="+", help="Shimeji names to export")
        self.exporter_parser.add_argument("--output", "-o", type=str, help="Output path")

        self.config_manager = self.subparsers.add_parser("config", help="Manage configuration")
        self.config_manager.add_argument("subaction", help="Action to perform", choices=["get", "set", "list"])
        self.config_manager.add_argument("key", help="Key to get/set", choices= ["breeding", "dragging", "ie-interactions", "ie-throwing", "cursor-position", "mascot-limit", "ie-throw-policy", "dismiss-animations", "mascot-interactions", "framerate"], nargs="?")
        self.config_manager.add_argument("value", help="Value to set", nargs="?")

        self.upgrade_parser = self.subparsers.add_parser("upgrade", help="Upgrade configs to latest version")

        self.set_behavior_parser = self.subparsers.add_parser("set-behavior", help="Set behavior for a shimeji")
        self.set_behavior_parser.add_argument("behavior", help="Behavior name")

        self.args = self.parser.parse_args()

        self.config_path = self.args.config_path
        self.shimeji_path = f'{self.config_path}/shimejis'

        os.makedirs(self.shimeji_path, exist_ok=True)

        self.known_shimejis: list[tuple[str,str,str]] = list(self.get_known_shimejis())

        self.overlay_process = None

    def get_known_shimejis(self):
        for name in os.listdir(self.shimeji_path):
            if os.path.isdir(f'{self.shimeji_path}/{name}'):
                if os.path.exists(f'{self.shimeji_path}/{name}/manifest.json'):
                    with open(f'{self.shimeji_path}/{name}/manifest.json', 'r') as f:
                        try:
                            manifest = json.load(f)
                        except json.JSONDecodeError:
                            continue
                        version = manifest.get("version", "0.0.0")
                        numberic_version = tuple(map(int, version.split(".")))
                        if numberic_version < (0,0,1) and self.args.action != "upgrade":
                            print(f"[WARNING] Shimeji {name} is outdated, consider upgrading by running 'shimejictl upgrade'")
                        names = (manifest.get("name", None), manifest.get("display_name", manifest.get("name", None)), f'{self.shimeji_path}/{name}')
                        if names[0] is not None:
                            yield names

    def upgrade(self):

        def upgrade_to_0_0_1(name):
            # Changes:
            # - Images now stored in QOI format
            # - Bump version to 0.0.1

            manifest = json.load(open(f'{self.shimeji_path}/{name}/manifest.json', 'r'))

            self.args.shimejis = [name]
            self.args.output = f"{self.shimeji_path}/{name}-{manifest.get('version')}.bak.wlshm"
            self.export_shimeji()
            print(f"[INFO] Backed up {name} to {self.args.output}")

            manifest["version"] = "0.0.1"

            for path, dirs, files in os.walk(f'{self.shimeji_path}/{name}'):
                for file in files:
                    if file.lower().endswith(".png"):
                        img = Image.open(f"{path}/{file}")
                        encode_img(img, False, f"{path}/{file[:-4]}.qoi")
                        os.remove(f"{path}/{file}")

            actions = open(f"{self.shimeji_path}/{name}/{manifest['actions']}").read()
            with open(f"{self.shimeji_path}/{name}/{manifest['actions']}", "w") as f:
                f.write(actions.replace('.png', '.qoi'))

            with open(f'{self.shimeji_path}/{name}/manifest.json', 'w') as f:
                json.dump(manifest, f, indent=4)

        for name in os.listdir(self.shimeji_path):
            if os.path.isdir(f'{self.shimeji_path}/{name}'):
                if os.path.exists(f'{self.shimeji_path}/{name}/manifest.json'):
                    with open(f'{self.shimeji_path}/{name}/manifest.json', 'r') as f:
                        try:
                            manifest = json.load(f)
                        except json.JSONDecodeError:
                            continue
                        version = manifest.get("version", "0.0.0")
                        numberic_version = tuple(map(int, version.split(".")))
                        if numberic_version < WL_SHIMEJI_FORMAT_VERSION_0_0_1:
                            print(f"[INFO] Upgrading shimeji {name} to version 0.0.1")
                            upgrade_to_0_0_1(name)

        print("Done.")

    def connect_to_overlay(self, extra_argv: list[str] = [], exclusive: bool = False, quiet: bool = True, do_not_start: bool = False):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        try:
            sock.connect(f'{os.environ.get("XDG_RUNTIME_DIR", "/tmp")}/shimeji-overlayd.sock')
            if exclusive:
                return None
        except (ConnectionRefusedError, FileNotFoundError):
            # Overlay not running, start it
            if do_not_start:
                return None
            sock, childsock = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            self.overlay_process = subprocess.Popen(['shimeji-overlayd', "-c", self.config_path, *extra_argv, "-cfd", str(childsock.fileno())], pass_fds=[childsock.fileno()],
                stdout=subprocess.DEVNULL if quiet else None,
                stderr=subprocess.DEVNULL if quiet else None,
                start_new_session=True
            )
            childsock.close()
            status = sock.recv(1025+8)
            if status[0] != 0x7f:
                if status[0] == 0xFF:
                    print("[ERROR] Overlay failed to start")
                    error_len = struct.unpack("@Q", status[1:9])[0]
                    error_msg = status[9:9+error_len].decode("utf-8")
                    for line in error_msg.split("\n"):
                        print(f"[ERROR] {line}")
                    raise SystemExit(1)
                else:
                    print("[ERROR] Overlay failed to start")
                    raise SystemExit(1)
        return sock

    def run(self):
        if self.args.action == 'summon':
            self.summon()
        elif self.args.action == 'dismiss':
            self.dismiss()
        elif self.args.action == 'stop':
            self.stop()
        elif self.args.action == 'load-config':
            self.load_config()
        elif self.args.action == 'info':
            self.info()
        elif self.args.action == 'foreground':
            self.foreground()
        elif self.args.action == 'list':
            self.list()
        elif self.args.action == "convert":
            self.convert()
        elif self.args.action == "import":
            self.import_shimeji()
        elif self.args.action == "export":
            self.export_shimeji()
        elif self.args.action == "config":
            self.configuration_manager()
        elif self.args.action == "set-behavior":
            self.set_behavior()
        elif self.args.action == "upgrade":
            self.upgrade()
        else:
            self.parser.print_help()

    def set_behavior(self):
        sock = self.connect_to_overlay(do_not_start=True)
        if sock is None:
            print("[ERROR] Overlay not running")
            raise SystemExit(1)

        selected_id = self.select(sock)
        if selected_id is None:
            print("[ERROR] No shimeji selected")
            raise SystemExit(1)

        message = b"\xc1"
        message += struct.pack("@I", selected_id)
        message += len(self.args.behavior).to_bytes(1, "big")
        message += self.args.behavior.encode("utf-8")

        sock.send(message)
        result = sock.recv(2)
        if result[0] != 0xc2:
            print("[ERROR] Invalid response from overlay")
            raise SystemExit(1)

        if result[1] != 0:
            match result[1]:
                case 1:
                    print("[ERROR] Protocol violation")
                case 2:
                    print("[ERROR] Selected shimeji not found")
                case 3:
                    print("[ERROR] Selected shimeji does not have the specified behavior")
                case _:
                    print("[ERROR] Unknown error")
            raise SystemExit(1)

        print("Behavior set successfully")


    def import_shimeji(self):
        def guess_format() -> str:
            if self.args.shimeji_ee:
                return "shimeji-ee"
            elif self.args.wl_shimeji:
                return "wl_shimeji"
            if os.path.isfile(self.args.path):
                if self.args.path.endswith(".wlshm"):
                    return "wl_shimeji"
                elif self.args.path.endswith(".zip"):
                    with zipfile.ZipFile(self.args.path, "r") as zipf:
                        if "pack_manifest.json" in zipf.namelist():
                            return "wl_shimeji"
                        else:
                            root_dirs = [name for name in zipf.namelist() if name.endswith("/") and name.count("/") == 1]
                            root_files = [name for name in zipf.namelist() if not name.count("/")]

                            if len(root_dirs) == 1 and not len(root_files):
                                if f"{root_dirs[0]}conf/" in zipf.namelist() and f"{root_dirs[0]}img/" in zipf.namelist():
                                    return "shimeji-ee"
                                else:
                                    return "unknown"
                            elif len(root_dirs) >= 2 and len(root_files) >= 1:
                                if "conf/" in root_dirs and "img/" in root_dirs:
                                    return "shimeji-ee"
                                else:
                                    return "unknown"
                            else:
                                return "unknown"
                    return "unknown"
            elif os.path.isdir(self.args.path):
                root_dirs = [name for name in os.listdir(self.args.path) if os.path.isdir(f"{self.args.path}/{name}")]
                root_files = [name for name in os.listdir(self.args.path) if os.path.isfile(f"{self.args.path}/{name}")]
                if len(root_dirs) == 1 and not len(root_files):
                    self.args.path = f"{self.args.path}/{root_dirs[0]}"
                elif os.path.exists(f"{self.args.path}/conf") and os.path.exists(f"{self.args.path}/img"):
                    return "shimeji-ee"
                else:
                    return "unknown"
            else:
                return "unknown"
            return "unknown"

        if self.args.shimeji_ee and self.args.wl_shimeji:
            print("Error: Both Shimeji-ee and wl_shimeji formats cannot be specified at the same time")
            raise SystemExit(1)

        format = guess_format()

        if format == "unknown":
            print("Provided path is not a valid Shimeji-ee instance or wl_shimeji pack")
            raise SystemExit(1)

        if format == "shimeji-ee":
            temp_dir = tempfile.mkdtemp()
            if os.path.isfile(self.args.path):
                with zipfile.ZipFile(self.args.path, "r") as zipf:
                    root_dirs = [name for name in zipf.namelist() if name.endswith("/") and name.count("/") == 1]
                    if len(root_dirs) == 1:
                        root_dir = root_dirs[0]
                    else:
                        root_dir = ""
                    zipf.extractall(temp_dir)
                    self.args.path = f"{temp_dir}/{root_dir}"


            names = [name for name in os.listdir(f"{self.args.path}/img") if os.path.isdir(f"{self.args.path}/img/{name}")]
            if not names:
                print("No shimejis found")
                raise SystemExit(1)

            print("Following shimejis found:")
            for i, name in enumerate(names):
                if name == "unused": continue
                print(f"{i}. {name}")

            print('\nList the shimejis you want to import separated by space. Type (A)ll to import all shimejis')
            selected = input("> ").replace(',', ' ').strip()

            if selected.lower() in ['a','all']:
                selected = names
            else:
                selected_ = []
                for i in selected.split():
                    try:
                        selected_.append(names[int(i)])
                    except:
                        if (i in names): selected_.append(i)

                selected = selected_


            for prototype in selected:
                if prototype == "unused": continue
                if os.path.isdir(f"{self.args.path}/img/{prototype}"):
                    if os.path.exists(f"{self.shimeji_path}/{prototype}"):
                        if not self.args.force:
                            print(f"[WARN] Shimeji {prototype} already exists, use -f to overwrite; Skipping...")
                            continue
                        else:
                            shutil.rmtree(f"{self.shimeji_path}/{prototype}", ignore_errors=True)

                    conf_dir = f"{self.args.path}/img/{prototype}/conf" if os.path.exists(f"{self.args.path}/img/{prototype}/conf") else f"{self.args.path}/conf"
                    self.args.root_dir = f"{self.args.path}/img/{prototype}"

                    # Compile the prototype
                    programs, actions, behaviors = Compiler.compile_shimeji(f"{self.args.path}/img/{prototype}", indent=4, conf_path = conf_dir)

                    manifest = {}

                    manifest["name"] = f"Shimeji.{pathlib.Path(self.args.root_dir).name}"

                    # TODO

                    # if self.args.author:
                    manifest["author"] = ""
                    # if self.args.version:
                    manifest["version"] = "0.0.1"
                    # if self.args.description:
                    manifest["description"] = ""
                    # if self.args.display_name:
                    manifest["display_name"] = pathlib.Path(self.args.root_dir).name
                    manifest["programs"] = "programs.json"
                    manifest["actions"] = "actions.json"
                    manifest["behaviors"] = "behaviors.json"
                    manifest["assets"] = "assets"

                    os.makedirs(f"{self.shimeji_path}/{prototype}/assets", exist_ok=True)
                    with open(f"{self.shimeji_path}/{prototype}/programs.json", "w") as f:
                        f.write(programs)
                    with open(f"{self.shimeji_path}/{prototype}/actions.json", "w") as f:
                        f.write(actions)
                    with open(f"{self.shimeji_path}/{prototype}/behaviors.json", "w") as f:
                        f.write(behaviors)
                    with open(f"{self.shimeji_path}/{prototype}/manifest.json", "w") as f:
                        f.write(json.dumps(manifest, indent=4))

                    for path, dirs, files in os.walk(self.args.root_dir):
                        for file in files:
                            if file.lower().endswith(".png"):
                                resulting_path: pathlib.Path = pathlib.Path(f"{self.shimeji_path}/{prototype}") / "assets" / pathlib.Path(path).relative_to(self.args.root_dir) / file
                                os.makedirs(resulting_path.parent, exist_ok=True)

                                img = Image.open(f"{path}/{file}")
                                encode_img(img, False, str(resulting_path)[:-4]+'.qoi')

                    print(f"Imported {prototype}")

            shutil.rmtree(temp_dir, ignore_errors=True)
        elif format == "wl_shimeji":
            with zipfile.ZipFile(self.args.path, "r") as zipf:
                pack_manifest = json.load(zipf.open("pack_manifest.json"))

                names = [name for name in pack_manifest["shimejis"]]
                if not names:
                    print("No shimejis found")
                    raise SystemExit(1)

                print("Following shimejis found:")
                for i, name in enumerate(names):
                    if name == "unused": continue
                    print(f"{i}. {name}")

                print('\nList the shimejis you want to import separated by space. Type (A)ll to import all shimejis')
                selected = input("> ").replace(',', ' ').strip()

                if selected.lower() in ['a','all']:
                    selected = names
                else:
                    selected_ = []
                    for i in selected.split():
                        try:
                            selected_.append(names[int(i)])
                        except:
                            if (i in names): selected_.append(i)

                    selected = selected_

                for shimeji in selected:
                    if os.path.exists(f"{self.shimeji_path}/{shimeji['name'].removeprefix('Shimeji.')}"):
                        if not self.args.force:
                            print(f"Shimeji {shimeji['name']} already exists, use -f to overwrite; Skipping...")
                            continue
                        else:
                            shutil.rmtree(f"{self.shimeji_path}/{shimeji['name'].removeprefix('Shimeji.')}", ignore_errors=True)

                    for file in zipf.namelist():
                        if file.startswith(f"prototypes/{shimeji['name'].removeprefix('Shimeji.')}/"):
                            if file.endswith('/'): continue
                            prototype_prefix = f"prototypes/{shimeji['name'].removeprefix('Shimeji.')}/"
                            filename = file.replace(prototype_prefix, "")
                            basedir = os.path.dirname(filename)
                            os.makedirs(f"{self.shimeji_path}/{shimeji['name'].removeprefix('Shimeji.')}/{basedir}", exist_ok=True)
                            with open(f"{self.shimeji_path}/{shimeji['name'].removeprefix('Shimeji.')}/{filename}", "wb") as f:
                                f.write(zipf.read(file))
                    print(f"Imported {shimeji['name']}")
        else:
            print("Unknown format")
            raise SystemExit(1)

    def export_shimeji(self):
        if not self.args.shimejis:
            print("No shimejis specified")
            raise SystemExit(1)

        if not self.args.output:
            self.args.output = os.curdir + "/export.wlshm"

        if os.path.isdir(self.args.output):
            self.args.output = f"{self.args.output}/export.wlshm"

        if self.args.shimejis == ["all"]:
            self.args.shimejis = [name[0] for name in self.known_shimejis]

        pack_manifest = {}
        pack_manifest["version"] = "0.0.1"
        pack_manifest["shimejis"] = []

        with zipfile.ZipFile(self.args.output, "w") as zipf:
            for prototype in self.args.shimejis:
                if not prototype.count("."):
                    prototype = f"Shimeji.{prototype}"

                dirname = prototype.removeprefix("Shimeji.")

                if prototype in [name[0] for name in self.known_shimejis]:
                    # Find prototype directory
                    prototype_dir = [name[2] for name in self.known_shimejis if name[0] == prototype][0]
                    zipf.write(prototype_dir, f"prototypes/{dirname}/")
                    for path, dirs, files in os.walk(prototype_dir):
                        for file in files:
                            zipf.write(f"{path}/{file}", f"prototypes/{dirname}/{path.removeprefix(prototype_dir)}/{file}")
                    pack_manifest["shimejis"].append(json.load(open(f"{prototype_dir}/manifest.json")))
                    continue

                print(f"Shimeji {prototype} not found")

            zipf.writestr("pack_manifest.json", json.dumps(pack_manifest, indent=4))

    def configuration_manager(self):
        param_list = ["breeding", "dragging", "ie-interactions", "ie-throwing", "cursor-position", "mascot-limit", "ie-throw-policy", "dismiss-animations", "mascot-interactions", "framerate"]

        if self.args.subaction == "list":
            for param in param_list:
                self.args.key = param
                self.args.subaction = "get"
                self.configuration_manager()
            return

        sock = self.connect_to_overlay(do_not_start=True)
        if sock is None:
            print("[ERROR] Overlay not running")
            raise SystemExit(1)

        if self.args.subaction == "get":
            if self.args.key not in param_list:
                print(f"[ERROR] Unknown parameter {self.args.key}")
                raise SystemExit(1)
            key_id = param_list.index(self.args.key)
            message = b"\xf1"+(key_id | 0x80).to_bytes(1, "little")
            sock.send(message)
            response = sock.recv(6)
            if response[0] != 0xf2:
                print("[ERROR] Invalid response")
                raise SystemExit(1)
            if response[1] != 0x80:
                if response[1] == 0x81:
                    print("[ERROR] Invalid parameter name")
                print("[ERROR] Failed to get parameter")
                raise SystemExit(1)
            if key_id in [0,1,2,3,4,7,8]:
                print(f"{self.args.key}: {bool(response[2])}".lower())
            elif key_id == 5:
                print(f"{self.args.key}: {int.from_bytes(response[2:6], 'little')}")
            elif key_id == 6:
                print(f"{self.args.key}: {['none', 'loop', 'stop_at_border', 'bounce', 'close', 'minimize'][response[2]]}")
            elif key_id == 9:
                print(f"{self.args.key}: {round(1000000/int.from_bytes(response[2:6], 'little'))}")
        elif self.args.subaction == "set":
            if self.args.key not in param_list:
                print(f"[ERROR] Unknown parameter {self.args.key}")
                raise SystemExit(1)
            key_id = param_list.index(self.args.key)

            value = None
            if key_id in [0,1,2,3,4,7,8]:
                if self.args.value not in ["true", "false"]:
                    print(f"[ERROR] Invalid value for parameter {self.args.key}")
                    raise SystemExit(1)
                value = self.args.value == "true"
            elif key_id == 5:
                if not self.args.value.isdigit():
                    print(f"[ERROR] Invalid value for parameter {self.args.key}")
                    raise SystemExit(1)
                value = int(self.args.value)
            elif key_id == 6:
                if self.args.value not in ["none", "loop", "stop_at_border", "bounce", "close", "minimize"]:
                    print(f"[ERROR] Invalid value for parameter {self.args.key}")
                    raise SystemExit(1)
                value = ["none", "loop", "stop_at_border", "bounce", "close", "minimize"].index(self.args.value.lower())
            elif key_id == 9:
                if not self.args.value.isdigit():
                    print(f"[ERROR] Invalid value for parameter {self.args.key}")
                    raise SystemExit(1)
                value = int(self.args.value)
                value = round(1000000/value)

            if value is None:
                print(f"[ERROR] Invalid value for parameter {self.args.key}")
                raise SystemExit(1)

            message = b"\xf1"
            message += key_id.to_bytes(1, "little")
            if isinstance(value, bool):
                message += int(value).to_bytes(1, "little")
            elif isinstance(value, int):
                message += value.to_bytes(4, "little")

            sock.send(message)
            response = sock.recv(2)
            if response[0] != 0xf2:
                print("[ERROR] Invalid response")
                raise SystemExit(1)
            if response[1] != 0:
                if response[1] == 0x1:
                    print("[ERROR] Invalid parameter name")
                print("[ERROR] Failed to set parameter")
                raise SystemExit(1)
            print("Parameter set successfully")

        else:
            print("Invalid subaction")

    def summon(self):
        extra_argv = []

        if self.args.shimeji == "all":
            extra_argv.append("-se")
            self.connect_to_overlay(extra_argv=extra_argv, exclusive=True)
            return
        if self.args.shimeji not in [name[0] for name in self.known_shimejis] and f"Shimeji.{self.args.shimeji}" not in [name[0] for name in self.known_shimejis]:
            print(f"[ERROR] Shimeji {self.args.shimeji} not found")
            raise SystemExit(1)
        message = b""

        if self.args.select:
            message = b"\x01"+len(self.args.shimeji).to_bytes(1, "big")+self.args.shimeji.encode("utf-8")
        else:
            message = b"\x0e"+len(self.args.shimeji).to_bytes(1, "big")+self.args.shimeji.encode("utf-8")

        sock = self.connect_to_overlay(extra_argv=extra_argv)
        if sock is None:
            return

        sock.send(message)
        response = sock.recv(2)

        if response[0] != 0x3:
            print("[ERROR] Failed to summon shimeji: Invalid response")
            raise SystemExit(1)

        if response[1] != 0:
            print("[ERROR] Failed to summon shimeji: ", end="")
            if response[1] == 0x1:
                print("Shimeji prototype not found")
            elif response[1] == 0x2:
                print("Somehow environment is NULL")
            elif response[1] == 0x3:
                print("Failed to create mascot")
            else:
                print("Unknown error")
            raise SystemExit(1)

    def dismiss(self):

        if self.args.all and self.args.all_other:
            print("[ERROR] Cannot dismiss all and all other at the same time")
            raise SystemExit(1)

        sock = self.connect_to_overlay(do_not_start=True)
        if sock is None:
            print("[ERROR] Overlay not running")
            raise SystemExit(1)

        message = b"\x0d"

        """
        0 - Dismiss selected
        1 - Dismiss all
        2 - Dismiss all other
        3 - Dismiss other of same type
        4 - Dismiss all of same type
        """

        if self.args.all:
            if self.args.same:
                message += b"\x04"
            else:
                message += b"\x01"
        elif self.args.all_other:
            if self.args.same:
                message += b"\x03"
            else:
                message += b"\x02"
        else:
            message += b"\x00"

        selected_id = self.select(sock)
        message += selected_id.to_bytes(4, "little")

        sock.send(message)
        status = sock.recv(2)

        if status[0] != 0x7:
            print("[ERROR] Failed to dismiss shimeji: Invalid response")
            raise SystemExit(1)

        if status[1] != 0:
            print("[ERROR] Failed to dismiss shimeji: ", end="")
            if status[1] == 0x1:
                print("Clicked not on mascot")
            else:
                print(f"Unknown error, code is {status[1]}")
            raise SystemExit(1)

    def stop(self):
        sock = self.connect_to_overlay(do_not_start=True)
        if sock is None:
            print("[ERROR] Overlay not running")
            raise SystemExit(1)

        message = b"\x04\x00"
        sock.send(message)
        status = sock.recv(2)

    def load_config(self):
        print("Not implemented")

    def information_by_id(self, id: int, sock) -> dict:

        def read_string(buffer: bytes) -> tuple[str, bytes]:
            string_len = int.from_bytes(buffer[:1], "little")
            buffer = buffer[1:]
            string = buffer[:string_len].decode("utf-8", errors="ignore")
            buffer = buffer[string_len:]
            return string, buffer

        def read_int(buffer: bytes) -> tuple[int, bytes]:
            integer = struct.unpack("@i", buffer[:4])[0]
            buffer = buffer[4:]
            return integer, buffer

        def read_float(buffer: bytes) -> tuple[float, bytes]:
            float_val = struct.unpack("@f", buffer[:4])[0]
            buffer = buffer[4:]
            return float_val, buffer

        def read_uint64(buffer: bytes) -> tuple[int, bytes]:
            long_val = struct.unpack("@Q", buffer[:8])[0]
            buffer = buffer[8:]
            return long_val, buffer

        def read_uint32(buffer: bytes) -> tuple[int, bytes]:
            uint32_val = struct.unpack("@I", buffer[:4])[0]
            buffer = buffer[4:]
            return uint32_val, buffer

        def read_uint16(buffer: bytes) -> tuple[int, bytes]:
            uint16_val = struct.unpack("@H", buffer[:2])[0]
            buffer = buffer[2:]
            return uint16_val, buffer

        message = b"\x07\x00"
        message += id.to_bytes(4, "little")
        sock.send(message)
        response = sock.recv(4096)
        if response[0] != 0x6:
            print("[ERROR] Invalid response")
            raise SystemExit(1)
        if response[1] != 0:
            print("[ERROR] Failed to get mascot information")
            raise SystemExit(1)

        response = response[2:]

        display_name, response = read_string(response)
        name, response = read_string(response)
        prototype_path, response = read_string(response)

        target_mascot, response = read_int(response)

        action_index, response = read_int(response)
        action_duration, response = read_int(response)
        action_tick, response = read_int(response)
        refcounter, response = read_int(response)

        action, response = read_string(response)
        behavior, response = read_string(response)
        affordance, response = read_string(response)

        state, response = read_uint32(response)

        actionstack = []
        actionindexstack = []

        action_stack_len = int.from_bytes(response[:1], "little")
        response = response[1:]
        for _ in range(action_stack_len):
            action_name, response = read_string(response)
            action_index, response = read_int(response)
            actionstack.append(action_name)
            actionindexstack.append(action_index)

        behaviorpool = []

        behavior_pool_len = int.from_bytes(response[:1], "little")
        response = response[1:]
        for _ in range(behavior_pool_len):
            behavior_name, response = read_string(response)
            behavior_frequency, response = read_uint64(response)
            behaviorpool.append((behavior_name, behavior_frequency))

        variables = []

        variables_count = int.from_bytes(response[:1], "little")
        response = response[1:]
        for _ in range(variables_count):
            value = response[:4]
            response = response[4:]

            kind = response[:1]
            response = response[1:]

            used = response[:1]
            response = response[1:]

            evaluated = response[:1]
            response = response[1:]

            has_prototype = response[:1]
            response = response[1:]

            program_id = -1
            if int.from_bytes(has_prototype, "little"):
                program_id = int.from_bytes(response[:2], "little")
                response = response[2:]
            if int.from_bytes(kind, "little"):
                value = struct.unpack("<f", value)[0]
            else:
                value = int.from_bytes(value, "little")
            variables.append((value, int.from_bytes(kind, "little"), int.from_bytes(used, "little"), int.from_bytes(evaluated, "little"), int.from_bytes(has_prototype, "little"), program_id))

        return {
            "display_name": display_name,
            "name": name,
            "prototype_path": prototype_path,
            "target_mascot": target_mascot,
            "action": action,
            "action_index": action_index,
            "action_duration": action_duration,
            "action_tick": action_tick,
            "refcounter": refcounter,
            "behavior": behavior,
            "affordance": affordance,
            "state": state,
            "actionstack": actionstack,
            "actionindexstack": actionindexstack,
            "behaviorpool": behaviorpool,
            "variables": variables
        }

    def info(self):
        sock = self.connect_to_overlay(do_not_start=True)
        if sock is None:
            print("[ERROR] Overlay not running")
            raise SystemExit(1)

        variable_names = [
            "X", "Y",
            "Target X",
            "Target Y",
            "Gravity",
            "Looking Right",
            "Air Drag X",
            "Air Drag Y",
            "Velocity X",
            "Velocity Y",
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            "IEOffset X",
            "IEOffset Y",
        ]

        state_names = [
            "None",
            "Stay",
            "Animate",
            "Move",
            "Fall",
            "Interact",
            "Jump",
            "Drag",
            "Resisting",
            "ScanMove",
            "ScanJump",
            "FallWithIE",
            "WalkWithIE",
            "ThrowIE"
        ]

        selected_id = self.select(sock)
        mascot = self.information_by_id(selected_id, sock)

        print(f"~~~ Mascot id {selected_id} ~~~")
        print(f"Type: {mascot['display_name']} ({mascot['name']})")
        print(f"Prototype path: {mascot['prototype_path']}")
        print(f"Referenced {mascot['refcounter']} times")

        if mascot['target_mascot'] != -1:
            target = self.information_by_id(mascot['target_mascot'], sock)
            print(f"Targeting mascot id {mascot['target_mascot']} aka {target['display_name']} ({target['name']})")

        print(f"Current action: {mascot['action']} ({mascot['action_index']}) until {mascot['action_duration']} tick")
        print(f"Current behavior: {mascot['behavior']}")
        print(f"Current affordance: {mascot['affordance'] or '(nil)'}")
        print(f"Current state: {state_names[mascot['state']]}")
        print("\nAction stack:")
        for index, action in enumerate(mascot['actionstack']):
            print(f"  {index}: {action} @ {mascot['actionindexstack'][index]}")
        print("\nBehavior pool:")
        for index, (behavior, frequency) in enumerate(mascot['behaviorpool']):
            print(f"  {index}: {behavior} ({frequency})")

        print("\nVariables:")
        for index, (value, kind, used, evaluated, has_prototype, program_id) in enumerate(mascot['variables']):
            if variable_names[index] is not None:
                print(f"  {variable_names[index]}: {value}")

    def foreground(self):
        sock = self.connect_to_overlay(exclusive=True, quiet=False)
        if sock is None:
            print("[ERROR] Overlay is already running")
            raise SystemExit(1)

        try:
            while True: time.sleep(1)
        except KeyboardInterrupt:
            sock.close()
            if self.overlay_process.wait(1) is None:
                self.overlay_process.terminate()
                if self.overlay_process.wait(1) is None:
                    self.overlay_process.kill()

    def list(self):
        for name in self.known_shimejis:
            print(f"{name[1]} (\"{name[0]}\")")

    def select(self, sock) -> int:
        """
            Select a shimeji, returns it's id
        """
        message = b"\x0f\x00"
        sock.send(message)
        response = sock.recv(6)
        if response[0] != 0x4:
            print("[ERROR] Invalid response")
            raise SystemExit(1)
        if response[1] != 0:
            print("[ERROR] Failed to select shimeji")
            raise SystemExit(1)

        return int.from_bytes(response[2:], "little")


    def convert(self):
        try:
            programs, actions, behaviors = Compiler.compile_shimeji(self.args.root_dir, indent=self.args.indent)
        except Exception as e:
            raise e
            print(f"Error: {e}")
            raise SystemExit(1)

        output_path = f"{self.shimeji_path}/{pathlib.Path(self.args.root_dir).name}"

        if self.args.output:
            output_path = self.args.output

        if os.path.exists(output_path) and not self.args.force:
            print("Following directory already exists, aborting...")
            raise SystemExit(1)
        elif os.path.exists(output_path) and self.args.force:
            shutil.rmtree(output_path, ignore_errors=True)

        os.makedirs(pathlib.Path(output_path), exist_ok=True)
        manifest = {}
        if self.args.name:
            manifest["name"] = self.args.name
        else:
            manifest["name"] = f"Shimeji.{pathlib.Path(self.args.root_dir).name}"
        if self.args.author:
            manifest["author"] = self.args.author
        if self.args.version:
            manifest["version"] = self.args.version
        if self.args.description:
            manifest["description"] = self.args.description
        if self.args.display_name:
            manifest["display_name"] = self.args.display_name
        manifest["programs"] = "programs.json" or self.args.custom_programs_path
        manifest["actions"] = "actions.json" or self.args.custom_actions_path
        manifest["behaviors"] = "behaviors.json" or self.args.custom_behaviors_path
        manifest["assets"] = self.args.assets_dir or "assets"

        with open(pathlib.Path(output_path) / "manifest.json", "w") as f:
            f.write(json.dumps(manifest, indent=self.args.indent))

        with open(pathlib.Path(output_path) / "programs.json", "w") as f:
            f.write(programs)

        with open(pathlib.Path(output_path) / "actions.json", "w") as f:
            f.write(actions)

        with open(pathlib.Path(output_path) / "behaviors.json", "w") as f:
            f.write(behaviors)

        os.makedirs(pathlib.Path(output_path) / self.args.assets_dir, exist_ok=True)
        for path, dirs, files in os.walk(self.args.root_dir):
            for file in files:
                if file.lower().endswith(".png"):
                    resulting_path: pathlib.Path = pathlib.Path(output_path) / self.args.assets_dir / pathlib.Path(path).relative_to(self.args.root_dir) / file
                    os.makedirs(resulting_path.parent, exist_ok=True)

                    img = Image.open(f"{path}/{file}")
                    encode_img(img, False, str(resulting_path)[:-4]+'.qoi')

        print(f"Compilation successful. Output files are in {output_path}")


if __name__ == "__main__":
    ShimejiCtl().run()
