#!/usr/bin/env python3
"""Render the real UI skin controls using an already-built MSVC game target.

Private copies of main.cpp and the Windows profile-path helper are compiled.
Other engine objects and asset directories are read-only inputs; test settings
and captures stay under a fresh output directory. No real MainMenu loop,
updater check, or Discord RPC is run. Production profile-path code is unchanged.
The output directory must be on NTFS so its asset directory junctions work.
The private main selects SDL's software renderer for the offline dummy backend.
"""
import argparse
import ctypes
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def tracking_records(path):
    records = []
    header, lines = None, []
    for line in path.read_text(encoding="utf-16").splitlines():
        if line.startswith("^"):
            if header is not None:
                records.append((header, " ".join(lines)))
            header, lines = line[1:], []
        elif header is not None and line.strip():
            lines.append(line)
    if header is not None:
        records.append((header, " ".join(lines)))
    return records


def windows_args(command):
    # CommandLineToArgvW preserves backslashes and MSVC's escaped /D strings;
    # POSIX shlex would silently corrupt Windows paths.
    count = ctypes.c_int()
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    split.restype = ctypes.POINTER(ctypes.c_wchar_p)
    result = split("tool " + command, ctypes.byref(count))
    if not result:
        raise OSError("Could not parse MSVC tracking command")
    try:
        return [result[index] for index in range(1, count.value)]
    finally:
        free = ctypes.windll.kernel32.LocalFree
        free.argtypes = [ctypes.c_void_p]
        free.restype = ctypes.c_void_p
        free(ctypes.cast(result, ctypes.c_void_p))


def toolchain_environment(tlog):
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        raise RuntimeError("Visual Studio vswhere.exe is required")
    states = list(tlog.glob("*.lastbuildstate"))
    version = re.search(r"VCToolsVersion=([^:]+)", states[0].read_text() if states else "")
    installations = subprocess.check_output([
        str(vswhere), "-all", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
    ], text=True, creationflags=subprocess.CREATE_NO_WINDOW).splitlines()
    matching = [Path(path) for path in installations if path.strip()
                and (not version or (Path(path) / "VC/Tools/MSVC" / version.group(1)).is_dir())]
    if not matching:
        raise RuntimeError("No Visual Studio installation has the recorded MSVC toolset")
    installation = matching[0]
    bootstrap = Path(installation) / "Common7/Tools/VsDevCmd.bat"
    if not bootstrap.is_file():
        raise RuntimeError("The selected Visual Studio installation has no VsDevCmd.bat")
    version_option = " -vcvars_ver=" + version.group(1) if version else ""
    command = f'call "{bootstrap}" -no_logo -arch=x64 -host_arch=x64{version_option} >nul && set'
    # Environment values may contain credentials: capture them privately and
    # never put this output into a diagnostic log or the printed summary.
    # cmd.exe does not interpret the backslash-escaped inner quotes that
    # subprocess.list2cmdline would add to a list-form /c argument.
    invocation = subprocess.list2cmdline([os.environ.get("COMSPEC", "cmd.exe")]) + ' /d /s /c "' + command + '"'
    result = subprocess.run(invocation, capture_output=True, text=True, check=False,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise RuntimeError("Visual Studio compiler environment initialization failed")
    environment = {key.upper(): value for key, value in os.environ.items()}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            environment[key.upper()] = value
    return environment


def remove_compile_outputs(args):
    result, index = [], 0
    while index < len(args):
        arg = args[index]
        lower = arg.lower()
        if lower.startswith(("/yu", "/yc", "/fp", "/fo", "/fd", "/fe")):
            if lower in ("/yu", "/yc", "/fp", "/fo", "/fd", "/fe"):
                index += 1
        elif lower.startswith("/fi"):
            value = arg[3:] if len(arg) > 3 else args[index + 1]
            if "cmake_pch." not in value.lower():
                result.append(arg)
                if len(arg) == 3:
                    index += 1
                    result.append(value)
            elif len(arg) == 3:
                index += 1
        elif lower.endswith((".cpp", ".cxx", ".cc", ".c")) and not lower.startswith(("/d", "/i")):
            pass
        else:
            result.append(arg)
        index += 1
    return result


def write_response(path, args):
    # MSVC treats each response-file line separately: options such as /D
    # cannot be separated from their following value by a newline.
    path.write_text(" ".join(subprocess.list2cmdline([arg]) for arg in args) + "\n", encoding="utf-8")


def asset_junction(destination, source):
    if not source.is_dir():
        raise RuntimeError("Built runtime asset directory is missing: " + str(source))
    if destination.exists():
        if destination.resolve() != source.resolve():
            raise RuntimeError("Refusing to replace an existing asset directory: " + str(destination))
        return
    # Directory junction creation needs no Administrator/symlink privilege.
    # Only create named links inside the owned diagnostic output; never move,
    # delete, copy over, or modify the real runtime asset directories.
    result = subprocess.run([
        os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", "mklink", "/J",
        str(destination), str(source),
    ], capture_output=True, text=True, check=False,
        creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise RuntimeError("Could not create a private asset junction; use an NTFS output directory. "
                           + result.stderr.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-windows-candidate")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--skins", choices=("0", "1"), nargs="+", default=["0", "1"])
    parser.add_argument("--sizes", nargs="+", default=["640x480", "854x480", "1280x720", "1920x1080"])
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--hud", action="store_true", help="Also capture the real selected-building HUD and pause window")
    parser.add_argument("--reuse-profile", action="store_true", help="Reuse one private sandbox cache across size cases, resetting its test INI")
    parser.add_argument("--prepare-only", action="store_true", help="Prepare private source/response files without compiling or running")
    parser.add_argument("--compile-only", action="store_true", help="Compile/link the private probe without launching it")
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("This diagnostic runner requires Windows and an MSVC build")
    build, out = args.build_dir.resolve(), args.output_dir.resolve()
    marker = out / ".classic-ui-probe-owned"
    if out in (ROOT, build, Path.home(), Path(out.anchor)):
        raise RuntimeError("Choose a dedicated diagnostic output directory")
    if out.exists() and any(out.iterdir()) and not marker.is_file():
        raise RuntimeError("Output directory is not empty and is not owned by this probe")
    out.mkdir(parents=True, exist_ok=True)
    marker.write_text("Classic UI skin diagnostic output\n")

    main_path = ROOT / "src/main.cpp"
    user_paths_source = ROOT / "src/misc/fnkdat.cpp"
    candidates = list((build / "src").glob(f"*.dir/{args.configuration}/*.tlog"))
    candidates += list(build.glob(f"*.dir/{args.configuration}/*.tlog"))
    selected = None
    for tlog in candidates:
        compile_log = tlog / "CL.command.1.tlog"
        link_log = tlog / "link.command.1.tlog"
        if not compile_log.is_file() or not link_log.is_file():
            continue
        compiles = [command for header, command in tracking_records(compile_log)
                    if str(main_path).casefold() in [part.casefold() for part in header.split("|")]]
        path_compiles = [command for header, command in tracking_records(compile_log)
                         if str(user_paths_source).casefold() in [part.casefold() for part in header.split("|")]]
        links = [command for _header, command in tracking_records(link_log)
                 if "/out:" in command.casefold() and re.search(r"(?:^|[\\/\s])main\.obj(?:\s|\"|$)", command, re.I)]
        if len(compiles) == 1 and len(path_compiles) == 1 and len(links) == 1:
            if selected is not None:
                raise RuntimeError("Multiple game target tracking logs matched; use a dedicated build directory")
            selected = (tlog, compiles[0], path_compiles[0], links[0])
    if selected is None:
        raise RuntimeError("No completed MSVC game target tracking logs contain main.cpp, fnkdat.cpp and main.obj")
    tlog, compile_command, path_compile_command, link_command = selected
    workdir = tlog.parents[2]
    runtime = build / "bin" / args.configuration
    if not (runtime / "dunecity.exe").is_file():
        raise RuntimeError("Build the dunecity target before running the probe")

    source = main_path.read_text()
    injection = "int menuResult = MainMenu().showMenu();"
    if source.count(injection) != 1:
        raise RuntimeError("Main-menu probe injection point changed")
    source = source.replace(injection, "int menuResult = runClassicUISkinProbe();")
    clamp = "clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);"
    if source.count(clamp) != 1:
        raise RuntimeError("Dummy-display window fixture injection point changed")
    source = source.replace(clamp, "(void)displayIndex; // Keep the requested diagnostic render size.")
    renderer_flags = "Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;"
    if source.count(renderer_flags) != 1:
        raise RuntimeError("Dummy-display renderer fixture injection point changed")
    source = source.replace(renderer_flags,
        "Uint32 rendererFlags = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;")
    source = source.replace("if(shouldPlayIntro && (bFirstInit==true))", "if(false && shouldPlayIntro && (bFirstInit==true))")
    source = source.replace("DiscordManager::instance().initialize();", "// No Discord RPC in this offline diagnostic.")
    fixture = ROOT / "tests/menu/classic-skin-probe.inc"
    if not fixture.is_file():
        raise RuntimeError("Classic UI skin renderer fixture is missing")
    position = source.index("int main(")
    source = source[:position] + f'#include "{fixture.as_posix()}"\n' + source[position:]
    # Keep the same common header content, but do not read/write a shared PCH.
    source = f'#include "{(ROOT / "IDE/VC/stdafx.h").as_posix()}"\n' + source
    private_source, private_obj = out / "classic-skin-main.cpp", out / "classic-skin-main.obj"
    binary = out / "classic-skin-probe.exe"
    private_source.write_text(source)
    compile_args = remove_compile_outputs(windows_args(compile_command))
    compile_args.extend(["/Y-", "/Fo" + str(private_obj), "/Fd" + str(out / "compile.pdb"), str(private_source)])

    # Older Windows fnkdat ignores DUNECITY_USERDIR. Add a fail-closed override
    # only to this private object: every FNKDAT_USER request requires the
    # explicitly supplied profile, with no route back to real AppData.
    paths = user_paths_source.read_text()
    windows_start = paths.index("int fnkdat(const _TCHAR*")
    posix_start = paths.index("int fnkdat(const char*")
    windows_paths = paths[windows_start:posix_start]
    folder_query = "if (dwFlags\n       && SHGetFolderPath"
    if windows_paths.count(folder_query) != 1:
        raise RuntimeError("Windows profile-path fixture injection point changed")
    override = '''const _TCHAR* probeUserDir = _tgetenv(_T("DUNECITY_USERDIR"));
   if (rawflags == FNKDAT_USER) {
      if (!probeUserDir || !probeUserDir[0]
          || !((probeUserDir[0] && probeUserDir[1] == _T(':'))
               || probeUserDir[0] == _T('\\\\') || probeUserDir[0] == _T('/'))
          || _tcslen(probeUserDir) >= static_cast<size_t>(total)) {
         errno = EINVAL;
         return -1;
      }
      FNKDAT_S(_tcsncpy(buffer, probeUserDir, len));
      FNKDAT_S(_tcsncat(buffer, _T("\\\\"), len));
   } else if (dwFlags
       && SHGetFolderPath'''
    windows_paths = windows_paths.replace(folder_query, override)
    paths = paths[:windows_start] + windows_paths + paths[posix_start:]
    paths = f'#include "{(ROOT / "IDE/VC/stdafx.h").as_posix()}"\n' + paths
    private_paths, private_paths_obj = out / "classic-skin-fnkdat.cpp", out / "classic-skin-fnkdat.obj"
    private_paths.write_text(paths)
    path_compile_args = remove_compile_outputs(windows_args(path_compile_command))
    path_compile_args.extend(["/Y-", "/Fo" + str(private_paths_obj),
                              "/Fd" + str(out / "profile-path.pdb"), str(private_paths)])
    link_args, replaced, paths_replaced = [], 0, 0
    for arg in windows_args(link_command):
        lower = arg.lower()
        if Path(arg).name.lower() == "main.obj":
            link_args.append(str(private_obj))
            replaced += 1
        elif Path(arg).name.lower() == "fnkdat.obj":
            link_args.append(str(private_paths_obj))
            paths_replaced += 1
        elif lower.startswith(("/out:", "/pdb:", "/implib:", "/ilk:", "/manifestfile:", "/map:", "/pgd:")):
            continue
        else:
            link_args.append(arg)
    if replaced != 1 or paths_replaced != 1:
        raise RuntimeError("The recorded link command must contain exactly one main.obj and fnkdat.obj")
    link_args.extend(["/OUT:" + str(binary), "/PDB:" + str(out / "classic-skin-probe.pdb"),
                      "/IMPLIB:" + str(out / "classic-skin-probe.lib"), "/INCREMENTAL:NO"])
    compile_rsp, path_compile_rsp, link_rsp = out / "compile.rsp", out / "profile-path.rsp", out / "link.rsp"
    write_response(compile_rsp, compile_args)
    write_response(path_compile_rsp, path_compile_args)
    write_response(link_rsp, link_args)
    if args.prepare_only:
        print("Prepared diagnostic source and response files; no build or runtime was executed.")
        return

    environment = toolchain_environment(tlog)
    cl = shutil.which("cl.exe", path=environment.get("PATH"))
    linker = shutil.which("link.exe", path=environment.get("PATH"))
    if not cl or not linker:
        raise RuntimeError("Visual Studio compiler/linker are unavailable")
    with (out / "build.log").open("w") as log:
        for command in ([cl, "@" + str(compile_rsp)], [cl, "@" + str(path_compile_rsp)],
                        [linker, "@" + str(link_rsp)]):
            subprocess.run(command, cwd=workdir, env=environment, stdout=log,
                           stderr=subprocess.STDOUT, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if args.compile_only:
        print("Compiled and linked the private diagnostic; no runtime was executed.")
        return
    for directory in ("data", "config", "mods", "imported_sprites"):
        asset_junction(out / directory, runtime / directory)
    environment["PATH"] = str(runtime) + os.pathsep + environment.get("PATH", "")

    for skin in args.skins:
        for size in args.sizes:
            match = re.fullmatch(r"([1-9][0-9]*)x([1-9][0-9]*)", size)
            if not match:
                raise RuntimeError("Sizes must use WIDTHxHEIGHT")
            width, height = map(int, match.groups())
            if not (640 <= width <= 3840 and 480 <= height <= 2160):
                raise RuntimeError("Diagnostic dimensions are outside the supported bounds")
            case = out / f"skin-{skin}-{size}"
            case.mkdir(parents=True, exist_ok=True)
            profile = out / "profile" if args.reuse_profile else case / "profile"
            profile.mkdir(parents=True, exist_ok=True)
            # Game's offline content pinning may select a private workshop
            # snapshot. Keep each test case on the bundled Vanilla base while
            # retaining only the sandbox's already-copied asset cache.
            (profile / "mods").mkdir(exist_ok=True)
            (profile / "mods/active_mod.txt").write_text("vanilla\n")
            # Only 480/600/768 are valid fixed presets. Automatic remains 1:1
            # at 1280x720; 1080p explicitly exercises the Native selection.
            interface_height = -1 if size == "1920x1080" else height if height in (480, 600, 768) else 0
            (profile / "Dune City.ini").write_text(
                f"[Video]\nPhysical Width = {width}\nPhysical Height = {height}\n"
                f"Width = {width}\nHeight = {height}\nInterface Height = {interface_height}\n"
                f"UI Skin = {skin}\nMenu Palette = 0\nFullscreen = false\nFrameLimit = false\nPreferred Zoom Level = 0\n"
                "[General]\nPlay Intro = false\nPlayer Name = UI skin tester\nDiagnostic Logs = true\n"
                '[Audio]\nPlay Music = false\nPlay SFX = false\n[Network]\nMetaServer = ""\n'
                'Relay Endpoint = ""\nDirect Endpoint = ""\n[Discord]\nWebhookUrl = ""\n')
            env = dict(environment, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER="dummy",
                       SDL_AUDIODRIVER="dummy", MENU_PROBE_OUT=str(case), MENU_PROBE_WIDTH=str(width),
                       MENU_PROBE_HEIGHT=str(height), MENU_PROBE_SKIN=skin)
            env.pop("MENU_PROBE_HUD", None)
            if args.hud:
                env["MENU_PROBE_HUD"] = "1"
            logpath = case / "run.log"
            with logpath.open("w") as log:
                subprocess.run([str(binary), "--window", "--showlog"], cwd=case, env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.timeout,
                               creationflags=subprocess.CREATE_NO_WINDOW)
            text = logpath.read_text(errors="replace")
            results = [line for line in text.splitlines() if "CLASSIC_UI_SKIN_PROBE_PASS:" in line]
            if len(results) != 1:
                raise RuntimeError("Missing unique renderer fixture pass marker; see " + str(logpath))
            print(f"skin={skin} {size}: " + results[0])


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("Classic UI skin probe failed: " + str(error), file=sys.stderr)
        sys.exit(1)
