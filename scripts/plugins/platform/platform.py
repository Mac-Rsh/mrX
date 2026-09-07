#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Platform command - Install and uninstall platform source files.

Loads one platform `.toml` file, expands copy rules, then installs selected
source files into the target output directory. Install saves the previous file
state, and uninstall restores that saved state.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import shutil
import tomllib

from framework.command import Command, CommandResult, CommandSpec
from framework.completion import FILE_COMPLETION, attach_path_completion
from framework.console import Console
from framework.context import CommandContext


@dataclass(frozen=True, slots=True)
class PlatformDir:
    """Resolved platform source and output directories."""

    source: Path
    output: Path


@dataclass(frozen=True, slots=True)
class CopyEntry:
    """One resolved copy operation."""

    source: Path
    target: Path


@dataclass(frozen=True, slots=True)
class SourceSet:
    """Resolved source files for one add rule."""

    files: list[tuple[Path, Path]]
    keep_structure: bool


@dataclass(frozen=True, slots=True)
class PlatformResult:
    """Resolved platform data used by the command."""

    name: str
    directory: PlatformDir
    entries: list[CopyEntry]


@dataclass(frozen=True, slots=True)
class PlatformReporter:
    """Per-invocation output services for the platform command."""

    console: Console
    cwd: Path

    def write(self, message: str) -> None:
        """Write one platform status line."""
        self.console.write(message)

    def format_path(self, path: Path) -> str:
        """Return a concise path relative to the session directory."""
        try:
            display_path = path.resolve().relative_to(self.cwd.resolve())
        except ValueError:
            display_path = path
        return display_path.as_posix()


class PlatformCommand(Command):
    """Built-in command for installing and uninstalling platform files."""

    STATE_DIR_NAME = ".platform"
    MANIFEST_NAME = "manifest.json"
    OLD_DIR_NAME = "previous"
    TMP_DIR_NAME = "tmp"
    OLD_MANIFEST_NAME = "manifest.json"
    FILES_DIR_NAME = "files"

    spec = CommandSpec(
        name="platform",
        description="Install, uninstall, or inspect platform files",
    )

    def configure(self, parser: ArgumentParser) -> None:
        """Declare platform command arguments."""
        parser.add_argument(
            "action",
            nargs="?",
            default="install",
            choices=("install", "uninstall", "status"),
            help="Operation to apply (default: install)",
        )
        attach_path_completion(parser.add_argument(
            "-i",
            "--input",
            help="Input platform '.toml' file path",
        ), FILE_COMPLETION)
        parser.add_argument(
            "--dry-run",
            action="store_true",
            help="Print the resolved plan without writing files",
        )
        parser.add_argument(
            "--force",
            action="store_true",
            help="Uninstall even when installed files changed after install",
        )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Execute the platform workflow."""
        reporter = PlatformReporter(context.console, context.cwd)
        try:
            if arguments.action == "install":
                return self._run_install(context, arguments, reporter)
            if arguments.action == "uninstall":
                return self._run_uninstall(context, arguments, reporter)
            if arguments.action == "status":
                return self._run_status(context, arguments, reporter)
        except ValueError as exc:
            return CommandResult.failure(str(exc))
        return CommandResult.failure(
            f"Unknown platform action: {arguments.action}"
        )

    @classmethod
    def _run_install(cls, context: CommandContext,
                     arguments: Namespace,
                     reporter: PlatformReporter) -> CommandResult:
        """Install one platform and save previous target file state."""
        if not arguments.input:
            return CommandResult.failure(
                "Missing platform file: -i/--input",
                code="MISSING_INPUT",
            )
        input_path = context.resolve_path(arguments.input)
        if not input_path.exists():
            return CommandResult.failure(
                f"Platform file not found: {arguments.input}"
            )

        result = cls._resolve_platform(input_path)
        if arguments.dry_run:
            cls._print_install_plan(result, reporter)
            return CommandResult.success()

        cls._install(result, reporter)
        reporter.write(f"Build: {result.directory.output}")
        return CommandResult.success()

    @classmethod
    def _run_uninstall(cls, context: CommandContext,
                       arguments: Namespace,
                       reporter: PlatformReporter) -> CommandResult:
        """Uninstall the current platform by restoring saved old state."""
        output_root = cls._parse_output_root(context, arguments)
        if output_root is None:
            return CommandResult.failure(
                "Failed to resolve platform output directory"
            )

        if arguments.dry_run:
            cls._print_uninstall_plan(output_root, reporter)
            return CommandResult.success()

        cls._uninstall(output_root, arguments.force, reporter)
        reporter.write(f"Build: {output_root}")
        return CommandResult.success()

    @classmethod
    def _run_status(cls, context: CommandContext,
                    arguments: Namespace,
                    reporter: PlatformReporter) -> CommandResult:
        """Print the current platform install status."""
        output_root = cls._parse_output_root(context, arguments)
        if output_root is None:
            return CommandResult.failure(
                "Failed to resolve platform output directory"
            )

        cls._print_status(output_root, reporter)
        return CommandResult.success()

    @classmethod
    def _parse_output_root(cls, context: CommandContext,
                           arguments: Namespace) -> Path | None:
        """Resolve the output root for uninstall or status."""
        if not arguments.input:
            return context.cwd.resolve()

        input_path = context.resolve_path(arguments.input)
        if not input_path.exists():
            context.console.write(
                f"Platform file not found: {arguments.input}")
            return None
        try:
            data = cls._load_platform(input_path)
            return cls._resolve_directory(input_path, data.get("dir")).output
        except ValueError as exc:
            context.console.write(str(exc))
            return None

    @classmethod
    def _resolve_platform(cls, toml_path: Path) -> PlatformResult:
        """Resolve one platform file into concrete copy entries."""
        data = cls._load_platform(toml_path)
        platform = data["platform"]
        directory = cls._resolve_directory(toml_path, data.get("dir"))
        entries_by_target: dict[str, CopyEntry] = {}

        for item in platform.get("add", []):
            for entry in cls._resolve_add_rule(directory, item["src"],
                                               item["dst"]):
                # Validate that the target path stays under the output directory
                cls._relative_to_output(directory.output, entry.target)
                entries_by_target[str(entry.target)] = entry

        return PlatformResult(
            name=platform["name"],
            directory=directory,
            entries=list(entries_by_target.values()),
        )

    @classmethod
    def _load_platform(cls, toml_path: Path) -> dict:
        """Load and validate one platform TOML file."""
        try:
            with toml_path.open("rb") as file:
                data = tomllib.load(file)
        except tomllib.TOMLDecodeError as exc:
            raise ValueError(f"Invalid TOML file: {toml_path} ({exc})") from exc

        platform = data.get("platform")
        if not isinstance(platform, dict):
            raise ValueError(f"Missing table: [platform] ({toml_path})")

        name = platform.get("name")
        if not isinstance(name, str) or not name.strip():
            raise ValueError(f"Missing field: platform.name ({toml_path})")

        directory = data.get("dir")
        if directory is not None:
            if not isinstance(directory, dict):
                raise ValueError(f"Invalid table: [dir] ({toml_path})")
            if not isinstance(directory.get("src"), str) or not isinstance(
                    directory.get("dst"), str):
                raise ValueError(f"Missing field: dir.src/dst ({toml_path})")

        add_rules = platform.get("add", [])
        if not isinstance(add_rules, list):
            raise ValueError(f"Invalid field: [[platform.add]] ({toml_path})")
        for item in add_rules:
            if not isinstance(item, dict):
                raise ValueError(f"Invalid add rule: {toml_path}")
            if not isinstance(item.get("src"), str) or not isinstance(
                    item.get("dst"), str):
                raise ValueError(
                    f"Missing field: platform.add.src/dst ({toml_path})")

        return data

    @classmethod
    def _resolve_directory(
            cls,
            toml_path: Path,
            directory: dict | None,
    ) -> PlatformDir:
        """Resolve the working directories for one platform file."""
        if directory is None:
            return PlatformDir(source=toml_path.parent, output=toml_path.parent)

        return PlatformDir(
            source=cls._resolve_dir_path(toml_path, directory["src"],
                                         "source dir"),
            output=cls._resolve_dir_path(toml_path, directory["dst"],
                                         "output dir"),
        )

    @classmethod
    def _resolve_dir_path(
            cls,
            toml_path: Path,
            value: str,
            description: str,
    ) -> Path:
        """Resolve one directory path from the input TOML file."""
        normalized = cls._normalize_relative_path(
            value,
            description,
            allow_dot=True,
            allow_parent=True,
        )
        return (toml_path.parent / normalized).resolve()

    @classmethod
    def _resolve_add_rule(
            cls,
            directory: PlatformDir,
            src: str,
            dst: str,
    ) -> list[CopyEntry]:
        """Resolve one add rule into concrete copy entries."""
        src_text = cls._normalize_relative_path(
            src,
            "source path",
            allow_dot=True,
            allow_parent=True,
        )
        dst_text = cls._normalize_relative_path(
            dst,
            "destination path",
            allow_dot=True,
            allow_parent=True,
        )
        sources = cls._resolve_sources(directory.source, src_text)
        target_root = (directory.output / dst_text).resolve()

        if len(sources.files) > 1 and cls._looks_like_file_path(target_root):
            raise ValueError(
                "Invalid destination path for multi-source rule: "
                f"{target_root}")

        entries: list[CopyEntry] = []
        for source_path, relative_path in sources.files:
            if sources.keep_structure:
                target_path = (target_root / relative_path).resolve()
            else:
                target_path = cls._resolve_single_file_target(
                    target_root, source_path)
            entries.append(CopyEntry(source=source_path, target=target_path))
        return entries

    @classmethod
    def _resolve_sources(cls, source_root: Path, src: str) -> SourceSet:
        """Resolve one source path into concrete files."""
        if cls._has_wildcard(src):
            return SourceSet(files=cls._resolve_glob_sources(source_root, src),
                             keep_structure=True)

        source_path = (source_root / src).resolve()
        if not source_path.exists():
            raise ValueError(f"Source path not found: {src}")
        if source_path.is_dir():
            return SourceSet(files=cls._walk_directory(source_path),
                             keep_structure=True)

        return SourceSet(files=[(source_path, Path())], keep_structure=False)

    @classmethod
    def _resolve_glob_sources(
            cls,
            source_root: Path,
            pattern: str,
    ) -> list[tuple[Path, Path]]:
        """Resolve one wildcard pattern into concrete files."""
        matches = sorted(source_root.glob(pattern))
        if not matches:
            raise ValueError(f"Source path not found: {pattern}")

        anchor = source_root / cls._glob_anchor(pattern)
        files: list[tuple[Path, Path]] = []
        for match in matches:
            if match.is_dir():
                for child, relative_path in cls._walk_directory(match):
                    files.append(
                        (child, match.relative_to(anchor) / relative_path))
                continue
            files.append((match.resolve(), match.relative_to(anchor)))
        return files

    @classmethod
    def _install(cls, result: PlatformResult,
                 reporter: PlatformReporter) -> None:
        """Install platform files and persist previous state for uninstall."""
        output_root = result.directory.output
        state_dir = cls._state_dir(output_root)
        manifest_path = cls._manifest_path(output_root)
        old_dir = cls._old_dir(output_root)
        tmp_dir = cls._tmp_dir(output_root)
        tmp_manifest_path = state_dir / f"{cls.MANIFEST_NAME}.tmp"

        if manifest_path.exists() or old_dir.exists():
            raise ValueError(
                f"Platform already installed: {state_dir}. "
                "Run `platform uninstall` first."
            )

        if tmp_dir.exists():
            raise ValueError(
                f"Incomplete platform state found: {tmp_dir}. "
                "Restore or move this backup before installing again."
            )
        tmp_dir.mkdir(parents=True)

        try:
            old_manifest = cls._backup_old_state(result, tmp_dir)
            cls._write_json(tmp_dir / cls.OLD_MANIFEST_NAME, old_manifest)

            files = cls._copy_files(result.entries, output_root, reporter)
            manifest = {
                "version": 1,
                "name": result.name,
                "old": cls.OLD_DIR_NAME,
                "files": files,
            }

            cls._write_json(tmp_manifest_path, manifest)
            tmp_dir.replace(old_dir)
            tmp_manifest_path.replace(manifest_path)
        except (Exception, KeyboardInterrupt):
            restore_dir = old_dir if old_dir.exists() else tmp_dir
            cls._restore_old_state(
                output_root, restore_dir, force=True, reporter=reporter)
            manifest_path.unlink(missing_ok=True)
            tmp_manifest_path.unlink(missing_ok=True)
            shutil.rmtree(tmp_dir, ignore_errors=True)
            shutil.rmtree(old_dir, ignore_errors=True)
            raise

    @classmethod
    def _uninstall(cls, output_root: Path, force: bool,
                   reporter: PlatformReporter) -> None:
        """Restore the saved old state and remove current platform state."""
        state_dir = cls._state_dir(output_root)
        manifest_path = cls._manifest_path(output_root)
        old_dir = cls._old_dir(output_root)
        if not manifest_path.exists():
            raise ValueError(f"Platform state not found: {state_dir}")
        if not old_dir.exists():
            raise ValueError(f"Platform previous state not found: {state_dir}")

        manifest = cls._read_json(manifest_path)
        changed = cls._changed_installed_files(
            output_root, manifest, reporter)
        if changed and not force:
            for path in changed:
                reporter.write(f"Skip: changed installed file: {path}")
            raise ValueError(
                "Use `--force` to uninstall changed installed files.")

        cls._restore_old_state(
            output_root, old_dir, force=True, reporter=reporter)
        manifest_path.unlink(missing_ok=True)
        shutil.rmtree(old_dir, ignore_errors=True)
        try:
            state_dir.rmdir()
        except OSError:
            pass

    @classmethod
    def _backup_old_state(cls, result: PlatformResult, old_dir: Path) -> dict:
        """Save all target file states before install."""
        files: list[dict[str, str]] = []
        output_root = result.directory.output
        backup_root = old_dir / cls.FILES_DIR_NAME

        for entry in result.entries:
            relative_path = cls._relative_to_output(output_root, entry.target)
            relative_text = relative_path.as_posix()
            if entry.target.exists():
                backup_path = backup_root / relative_path
                backup_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(entry.target, backup_path)
                files.append({
                    "path": relative_text,
                    "state": "saved",
                    "backup": f"{cls.FILES_DIR_NAME}/{relative_text}",
                    "hash": cls._sha256(entry.target),
                })
            else:
                files.append({
                    "path": relative_text,
                    "state": "missing",
                })

        return {
            "version": 1,
            "name": result.name,
            "created": datetime.now(timezone.utc).isoformat(),
            "files": files,
        }

    @classmethod
    def _restore_old_state(
            cls,
            output_root: Path,
            old_dir: Path,
            force: bool,
            reporter: PlatformReporter,
    ) -> None:
        """Restore one saved old state directory."""
        old_manifest_path = old_dir / cls.OLD_MANIFEST_NAME
        if not old_manifest_path.exists():
            return

        old_manifest = cls._read_json(old_manifest_path)
        for item in old_manifest.get("files", []):
            target_path = cls._safe_output_path(output_root, item.get("path"))
            state = item.get("state")

            if state == "saved":
                backup_path = old_dir / item.get("backup", "")
                if not backup_path.exists():
                    raise ValueError(f"Backup file not found: {backup_path}")
                if target_path.is_dir():
                    if not force:
                        raise ValueError(
                            f"Target path is a directory: {target_path}")
                    shutil.rmtree(target_path)
                target_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(backup_path, target_path)
                reporter.write(
                    f"Restore: {reporter.format_path(target_path)}")
                continue

            if state == "missing":
                if target_path.is_dir():
                    if not force:
                        raise ValueError(
                            f"Target path is a directory: {target_path}")
                    shutil.rmtree(target_path)
                    reporter.write(
                        f"Delete: {reporter.format_path(target_path)}")
                elif target_path.exists() and (force or target_path.is_file()):
                    target_path.unlink()
                    reporter.write(
                        f"Delete: {reporter.format_path(target_path)}")
                cls._clean_empty_parents(output_root, target_path.parent)
                continue

            raise ValueError(f"Invalid old state entry: {item}")

    @classmethod
    def _changed_installed_files(
            cls,
            output_root: Path,
            manifest: dict,
            reporter: PlatformReporter,
    ) -> list[str]:
        """Return installed files whose hash differs from the manifest."""
        changed: list[str] = []
        for item in manifest.get("files", []):
            target_path = cls._safe_output_path(output_root, item.get("path"))
            expected_hash = item.get("hash")
            if not isinstance(expected_hash, str):
                continue
            if not target_path.is_file():
                changed.append(reporter.format_path(target_path))
                continue
            if cls._sha256(target_path) != expected_hash:
                changed.append(reporter.format_path(target_path))
        return changed

    @classmethod
    def _copy_files(
            cls,
            entries: list[CopyEntry],
            output_root: Path,
            reporter: PlatformReporter,
    ) -> list[dict]:
        """Copy all resolved platform files into the output directory."""
        copied_files: list[dict] = []
        for entry in entries:
            copied_files.append(
                cls._copy_one_file(entry, output_root, reporter))
        return copied_files

    @classmethod
    def _copy_one_file(cls, entry: CopyEntry, output_root: Path,
                       reporter: PlatformReporter) -> dict:
        """Copy one resolved source file to its target path."""
        if not entry.source.exists():
            raise ValueError(f"Source path not found: {entry.source}")

        relative_path = cls._relative_to_output(output_root, entry.target)
        entry.target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry.source, entry.target)
        source = reporter.format_path(entry.source)
        target = reporter.format_path(entry.target)
        reporter.write(f"Copy: {source} -> {target}")
        return {
            "path": relative_path.as_posix(),
            "hash": cls._sha256(entry.target),
        }

    @classmethod
    def _print_install_plan(cls, result: PlatformResult,
                            reporter: PlatformReporter) -> None:
        """Print the resolved platform install plan."""
        reporter.write(f"Platform: {result.name}")
        reporter.write(
            f"Source dir: {reporter.format_path(result.directory.source)}")
        reporter.write(
            f"Output dir: {reporter.format_path(result.directory.output)}")
        state_dir = cls._state_dir(result.directory.output)
        reporter.write(f"State dir: {reporter.format_path(state_dir)}")
        for entry in result.entries:
            if entry.target.exists():
                reporter.write(f"Save: {reporter.format_path(entry.target)}")
            source = reporter.format_path(entry.source)
            target = reporter.format_path(entry.target)
            reporter.write(f"Copy: {source} -> {target}")

    @classmethod
    def _print_uninstall_plan(cls, output_root: Path,
                              reporter: PlatformReporter) -> None:
        """Print the resolved platform uninstall plan."""
        state_dir = cls._state_dir(output_root)
        manifest_path = cls._manifest_path(output_root)
        old_dir = cls._old_dir(output_root)
        if not manifest_path.exists():
            raise ValueError(f"Platform state not found: {state_dir}")
        if not old_dir.exists():
            raise ValueError(f"Platform previous state not found: {state_dir}")

        old_manifest = cls._read_json(old_dir / cls.OLD_MANIFEST_NAME)
        reporter.write(f"Output dir: {reporter.format_path(output_root)}")
        for item in old_manifest.get("files", []):
            target_path = cls._safe_output_path(output_root, item.get("path"))
            if item.get("state") == "saved":
                reporter.write(
                    f"Restore: {reporter.format_path(target_path)}")
            elif item.get("state") == "missing":
                reporter.write(
                    f"Delete: {reporter.format_path(target_path)}")

    @classmethod
    def _print_status(cls, output_root: Path,
                      reporter: PlatformReporter) -> None:
        """Print the current platform install status."""
        state_dir = cls._state_dir(output_root)
        manifest_path = cls._manifest_path(output_root)
        reporter.write(f"Output dir: {reporter.format_path(output_root)}")
        if not manifest_path.exists():
            reporter.write("Platform: not installed")
            return

        manifest = cls._read_json(manifest_path)
        files = manifest.get("files", [])
        reporter.write(f"Platform: {manifest.get('name', 'unknown')}")
        reporter.write(f"State dir: {reporter.format_path(state_dir)}")
        reporter.write(f"Files: {len(files)}")
        changed = cls._changed_installed_files(
            output_root, manifest, reporter)
        if changed:
            reporter.write(f"Changed: {len(changed)}")

    @classmethod
    def _state_dir(cls, output_root: Path) -> Path:
        """Return the private platform state directory."""
        return output_root / cls.STATE_DIR_NAME

    @classmethod
    def _manifest_path(cls, output_root: Path) -> Path:
        """Return the platform state manifest path."""
        return cls._state_dir(output_root) / cls.MANIFEST_NAME

    @classmethod
    def _old_dir(cls, output_root: Path) -> Path:
        """Return the previous state backup directory."""
        return cls._state_dir(output_root) / cls.OLD_DIR_NAME

    @classmethod
    def _tmp_dir(cls, output_root: Path) -> Path:
        """Return the temporary state directory."""
        return cls._state_dir(output_root) / cls.TMP_DIR_NAME

    @staticmethod
    def _walk_directory(source_path: Path) -> list[tuple[Path, Path]]:
        """Collect all files under one source directory."""
        files: list[tuple[Path, Path]] = []
        for child in source_path.rglob("*"):
            if child.is_file():
                files.append((child, child.relative_to(source_path)))
        return files

    @staticmethod
    def _has_wildcard(value: str) -> bool:
        """Return whether the path string contains glob metacharacters."""
        return any(char in value for char in "*?[")

    @staticmethod
    def _glob_anchor(value: str) -> Path:
        """Return the static prefix path before the first wildcard part."""
        parts: list[str] = []
        for part in Path(value).parts:
            if any(char in part for char in "*?["):
                break
            parts.append(part)
        return Path(*parts) if parts else Path(".")

    @staticmethod
    def _looks_like_file_path(path: Path) -> bool:
        """Return whether the destination path looks like a single file path."""
        return path.name not in ("", ".") and path.suffix != ""

    @staticmethod
    def _resolve_single_file_target(target_root: Path,
                                    source_path: Path) -> Path:
        """Resolve a single source file target path."""
        if target_root.exists() and target_root.is_dir():
            return (target_root / source_path.name).resolve()
        return target_root

    @staticmethod
    def _normalize_relative_path(
            value: str,
            description: str,
            allow_dot: bool = False,
            allow_parent: bool = False,
    ) -> str:
        """Normalize and validate one relative path string."""
        normalized = value.replace("\\", "/").strip()
        if not normalized:
            raise ValueError(f"Invalid {description}: {value}")

        path = Path(normalized)
        if path.is_absolute():
            raise ValueError(f"Invalid {description}: {value}")
        if not allow_parent and any(part == ".." for part in path.parts):
            raise ValueError(f"Invalid {description}: {value}")
        if normalized == "." and not allow_dot:
            raise ValueError(f"Invalid {description}: {value}")
        return path.as_posix()

    @staticmethod
    def _clean_empty_parents(output_root: Path, path: Path) -> None:
        """Remove empty parent directories under the output directory."""
        current = path
        while current != output_root and current.is_relative_to(output_root):
            try:
                current.rmdir()
            except OSError:
                break
            current = current.parent

    @staticmethod
    def _relative_to_output(output_root: Path, target_path: Path) -> Path:
        """Return target_path relative to output_root or reject it."""
        try:
            return target_path.resolve().relative_to(output_root.resolve())
        except ValueError as exc:
            raise ValueError(
                f"Target path escapes output dir: {target_path}") from exc

    @staticmethod
    def _safe_output_path(output_root: Path, value: object) -> Path:
        """Resolve one manifest path under output_root."""
        if not isinstance(value, str) or not value:
            raise ValueError(f"Invalid manifest path: {value}")
        path = Path(value)
        if path.is_absolute() or any(part == ".." for part in path.parts):
            raise ValueError(f"Invalid manifest path: {value}")
        target_path = (output_root / path).resolve()
        PlatformCommand._relative_to_output(output_root, target_path)
        return target_path

    @staticmethod
    def _sha256(path: Path) -> str:
        """Calculate one file SHA-256 digest."""
        digest = hashlib.sha256()
        with path.open("rb") as file:
            for chunk in iter(lambda: file.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    @staticmethod
    def _read_json(path: Path) -> dict:
        """Read one JSON object file."""
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except OSError as exc:
            raise ValueError(
                f"Failed to read JSON file: {path} ({exc})") from exc
        except json.JSONDecodeError as exc:
            raise ValueError(f"Invalid JSON file: {path} ({exc})") from exc
        if not isinstance(data, dict):
            raise ValueError(f"Invalid JSON object: {path}")
        return data

    @staticmethod
    def _write_json(path: Path, data: dict) -> None:
        """Write one JSON object file."""
        path.parent.mkdir(parents=True, exist_ok=True)
        content = json.dumps(data, indent=2, ensure_ascii=False)
        path.write_text(f"{content}\n", encoding="utf-8")
