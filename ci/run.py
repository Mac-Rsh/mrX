#!/usr/bin/env python3
"""mrX local & CI full test runner (Windows host + MinGW GNU ld sections)."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent


def run_cmd(cmd: list[str], env: dict[str, str] | None = None,
            cwd: Path = ROOT, capture: bool = False) -> subprocess.CompletedProcess[str]:
    print(f"[ci] $ {' '.join(cmd)}", flush=True)
    res = subprocess.run(
        cmd,
        cwd=str(cwd),
        env=env,
        text=True,
        capture_output=capture,
    )
    if res.returncode != 0:
        if capture:
            if res.stdout:
                print(res.stdout, end="")
            if res.stderr:
                print(res.stderr, end="", file=sys.stderr)
        raise SystemExit(res.returncode)
    return res


def prepare_mingw_env(cc_arg: str) -> tuple[str, str, dict[str, str]]:
    env = os.environ.copy()
    cc_path = shutil.which(cc_arg)
    if not cc_path:
        for candidate in (
            r"C:\msys64\ucrt64\bin\gcc.exe",
            r"C:\msys64\mingw64\bin\gcc.exe",
        ):
            if Path(candidate).is_file():
                cc_path = candidate
                break
    if not cc_path:
        raise SystemExit(f"[ci] ERROR: compiler '{cc_arg}' not found in PATH")

    cc_dir = str(Path(cc_path).resolve().parent)
    env["PATH"] = cc_dir + os.pathsep + env.get("PATH", "")

    ld_path = shutil.which("ld", path=cc_dir) or shutil.which("ld", path=env["PATH"])
    if not ld_path:
        raise SystemExit("[ci] ERROR: GNU 'ld' not found alongside gcc")
    return cc_path, ld_path, env


def export_default_ld_script(ld_path: str, out_lds: Path,
                             env: dict[str, str]) -> None:
    res = run_cmd([ld_path, "--verbose"], env=env, capture=True)
    lines = res.stdout.splitlines()
    sep_indices = [i for i, line in enumerate(lines) if line.startswith("==========")]
    if len(sep_indices) < 2:
        raise SystemExit("[ci] ERROR: failed to parse default linker script from 'ld --verbose'")
    script_body = "\n".join(lines[sep_indices[0] + 1 : sep_indices[1]]) + "\n"
    out_lds.parent.mkdir(parents=True, exist_ok=True)
    out_lds.write_text(script_body, encoding="utf-8")


def collect_sources(generated_c: list[Path]) -> list[Path]:
    srcs: list[Path] = []
    for folder in ("kernel", "libc", "drivers", "fs", "modules", "platform", "test"):
        base = ROOT / folder
        if base.is_dir():
            srcs.extend(sorted(base.rglob("*.c")))
    srcs.extend(generated_c)
    srcs.append(ROOT / "ci" / "win_sim_test.c")
    srcs.append(ROOT / "ci" / "main.c")
    # Deduplicate preserving order
    seen: set[Path] = set()
    unique: list[Path] = []
    for p in srcs:
        rp = p.resolve()
        if rp not in seen:
            seen.add(rp)
            unique.append(p)
    return unique


def compile_one(cc: str, src: Path, obj_dir: Path, inc_dirs: list[Path],
                cflags: list[str], env: dict[str, str]) -> tuple[Path, Path, int, str]:
    rel = str(src.resolve())
    digest = hashlib.sha1(rel.encode("utf-8")).hexdigest()[:10]
    obj = obj_dir / f"{src.stem}_{digest}.o"
    cmd = [cc, "-c", *cflags]
    for inc in inc_dirs:
        cmd.extend(["-I", str(inc)])
    cmd.extend([str(src), "-o", str(obj)])
    res = subprocess.run(cmd, cwd=str(ROOT), env=env, text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return src, obj, res.returncode, res.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description="mrX CI full test runner")
    parser.add_argument("--cc", default="gcc", help="MinGW GCC executable (default: gcc)")
    parser.add_argument("--config", default="ci/win_test.config",
                        help="Kconfig .config preset (default: ci/win_test.config)")
    parser.add_argument("--build-dir", default="build/ci",
                        help="Build output directory (default: build/ci)")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 4,
                        help="Parallel compile jobs")
    parser.add_argument("--skip-scripts", action="store_true",
                        help="Skip Python scripts/tests")
    parser.add_argument("--only-scripts", action="store_true",
                        help="Only run Python scripts/tests")
    parser.add_argument("--keep-platform", action="store_true",
                        help="Keep installed arch/win in platform/ after run")
    parser.add_argument("--pthread", action="store_true", default=True,
                        help="Enable MR_USE_TEST_PTHREAD concurrency tests (default: on)")
    parser.add_argument("--no-pthread", dest="pthread", action="store_false",
                        help="Disable MR_USE_TEST_PTHREAD concurrency tests")
    parser.add_argument("--suite", help="Run a single MR_TEST suite")
    parser.add_argument("--case", nargs=2, metavar=("SUITE", "NAME"),
                        help="Run a single MR_TEST case")
    args = parser.parse_args()

    # 1. Host Python CLI tests
    if not args.skip_scripts:
        print("\n=== [1/4] Host CLI unit tests ===", flush=True)
        run_cmd([sys.executable, "-m", "unittest", "discover", "-s", "scripts/tests", "-v"])
        if args.only_scripts:
            return 0

    cc_path, ld_path, env = prepare_mingw_env(args.cc)
    build_dir = (ROOT / args.build_dir).resolve()
    gen_inc = build_dir / "include"
    obj_dir = build_dir / "obj"
    obj_dir.mkdir(parents=True, exist_ok=True)
    # Keep compiler temporary files inside build_dir for sandbox compatibility
    tmp_dir = build_dir / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    env["TMP"] = str(tmp_dir)
    env["TEMP"] = str(tmp_dir)

    platform_was_installed = (ROOT / "platform" / "platform.c").is_file()

    try:
        print("\n=== [2/4] Install arch/win & generate headers/linker script ===", flush=True)
        if not platform_was_installed:
            run_cmd([sys.executable, "scripts/cli.py", "platform", "install",
                     "-i", "arch/win/install.toml"], env=env)

        # Copy config preset into build_dir so kconfig does not dirty repo root
        cfg_copy = build_dir / ".config"
        shutil.copyfile(ROOT / args.config, cfg_copy)

        run_cmd([
            sys.executable, "scripts/cli.py", "kconfig", "--no-ui",
            "-c", str(cfg_copy),
            "-o", str(gen_inc / "mrX" / "mr_config.h"),
        ], env=env)

        run_cmd([
            sys.executable, "scripts/cli.py", "htc",
            "-i", "test/drivers/fixtures/driver.hts",
            "-o", str(gen_inc / "mrX" / "mr_hardgen.h"),
        ], env=env)

        rpc_idl_c = build_dir / "rpc_idl.c"
        run_cmd([
            sys.executable, "scripts/cli.py", "rpc-idl",
            "-i", "test/modules/rpc/rpc.idl",
            "-o", str(gen_inc / "mrX" / "modules" / "rpc" / "mr_rpc_idl.h"),
            "-c", str(rpc_idl_c),
            "--no-cpp",
        ], env=env)

        lds_path = build_dir / "win.lds"
        export_default_ld_script(ld_path, lds_path, env)
        run_cmd([
            sys.executable, "scripts/cli.py", "sections",
            "-i", str(lds_path),
        ], env=env)

        print("\n=== [3/4] Compile & link full test binary ===", flush=True)
        srcs = collect_sources([rpc_idl_c])
        inc_dirs = [gen_inc, ROOT / "include"]
        cflags = ["-O0", "-g", "-Wall", "-Wextra", "-Wno-sign-compare",
                  "-Wno-unused-parameter"]
        if args.pthread:
            cflags.append("-DMR_USE_TEST_PTHREAD")

        objs: list[Path] = []
        failed = False
        print(f"[ci] Compiling {len(srcs)} translation units with {args.jobs} jobs...", flush=True)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futs = [
                pool.submit(compile_one, cc_path, s, obj_dir, inc_dirs, cflags, env)
                for s in srcs
            ]
            for fut in as_completed(futs):
                src, obj, rc, out = fut.result()
                if out:
                    print(out, end="")
                if rc != 0:
                    print(f"[ci] FAILED compiling {src}", file=sys.stderr)
                    failed = True
                else:
                    objs.append(obj)

        if failed:
            return 1

        exe_path = build_dir / "mrx_ci_test.exe"
        # Use response file to stay well under Windows command-line length limits
        rsp_path = build_dir / "link.rsp"
        rsp_lines = [str(o).replace("\\", "/") for o in sorted(objs)]
        rsp_path.write_text("\n".join(rsp_lines) + "\n", encoding="utf-8")

        link_cmd = [
            cc_path,
            f"@{rsp_path}",
            f"-Wl,-T,{lds_path}",
            "-lws2_32",
        ]
        if args.pthread:
            link_cmd.append("-lpthread")
        link_cmd.extend(["-o", str(exe_path)])
        run_cmd(link_cmd, env=env)

        print("\n=== [4/4] Run full mrX C test suite ===", flush=True)
        test_cmd = [str(exe_path)]
        if args.case:
            test_cmd.extend(args.case)
        elif args.suite:
            test_cmd.append(args.suite)
        run_cmd(test_cmd, env=env, cwd=build_dir)
        return 0
    finally:
        if (not platform_was_installed) and (not args.keep_platform):
            subprocess.run(
                [sys.executable, "scripts/cli.py", "platform", "uninstall",
                 "-i", "arch/win/install.toml", "--force"],
                cwd=str(ROOT),
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )


if __name__ == "__main__":
    raise SystemExit(main())
