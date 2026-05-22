#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Platform command - Assemble platform source files from one TOML description.

Loads one platform `.toml` file, expands copy rules, then copies the selected
source files into the target output directory.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace
from dock import PluginSpec, export_plugin
from dataclasses import dataclass
from pathlib import Path
import tomllib
import shutil


@dataclass(frozen=True)
class PlatformDir:
    """Resolved platform source and output directories."""

    source: Path
    output: Path


@dataclass(frozen=True)
class CopyEntry:
    """One resolved copy operation."""

    source: Path
    target: Path


@dataclass(frozen=True)
class SourceSet:
    """Resolved source files for one add rule."""

    files: list[tuple[Path, Path]]
    keep_structure: bool


@dataclass(frozen=True)
class PlatformResult:
    """Resolved platform data used by the command."""

    name: str
    directory: PlatformDir
    entries: list[CopyEntry]


@export_plugin
class PlatformCommand:
    """Built-in command for assembling platform source files."""

    MANIFEST_NAME = ".platform_manifest"

    spec = PluginSpec(
        name="platform",
        description="Assemble platform files from a TOML file.",
    )

    def help(self) -> str:
        """Return detailed help text for the platform command.

        Returns:
            Human-readable help text for the command.
        """
        # Reuse the runtime parser
        parser = self._build_parser()
        lines = [
            "Usage: python ./scripts/cli.py platform -i <platform.toml> [options]",
            "Description: Assemble platform files from one TOML description.",
            "Aliases: (none)",
            "",
            parser.format_help().rstrip(),
            "Source and output directories are defined in the input TOML file.",
        ]
        return "\n".join(lines)

    def run(self, args: list[str]) -> int:
        """Execute the platform assembly workflow.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Parse command line arguments
        options = self._parse_args(args)
        if options is None:
            return 1

        # Resolve the command input path
        input_path = self._parse_input_path(options)
        if input_path is None:
            return 1

        # Resolve all copy entries first
        try:
            result = self._resolve_platform(input_path)
        except ValueError as exc:
            print(exc)
            return 1

        # Print the resolved plan only in dry-run mode
        if options.dry_run:
            self._print_plan(result)
            return 0

        # Clean the previous managed files before writing new ones
        manifest_path = result.directory.output / self.MANIFEST_NAME
        self._clean_output(result.directory.output, manifest_path)

        # Copy files and refresh the manifest
        try:
            copied_files = self._copy_files(result.entries)
            self._write_manifest(manifest_path, copied_files)
        except ValueError as exc:
            print(exc)
            return 1

        # Report the final output directory
        print(f"Build: {result.directory.output}")
        return 0

    @classmethod
    def _build_parser(cls) -> ArgumentParser:
        """Build the platform command line argument parser.

        Returns:
            Configured argument parser for the platform command.
        """
        # Keep the parser minimal
        parser = ArgumentParser(
            prog="python ./scripts/cli.py platform",
            add_help=True,
        )
        parser.add_argument(
            "-i",
            "--input",
            required=True,
            help="Input platform '.toml' file path",
        )
        parser.add_argument(
            "--dry-run",
            action="store_true",
            help="Print the resolved copy plan without writing files.",
        )
        return parser

    @classmethod
    def _parse_args(cls, args: list[str]) -> Namespace | None:
        """Parse platform command line arguments.

        Args:
            args: Raw command line arguments passed to the command.

        Returns:
            Parsed argument namespace, or `None` when parsing fails.
        """
        # Reuse the platform parser
        parser = cls._build_parser()

        try:
            return parser.parse_args(args)
        except SystemExit:
            # Argparse already printed the error
            return None

    @classmethod
    def _parse_input_path(cls, args: Namespace) -> Path | None:
        """Parse and validate the command input path.

        Args:
            args: Parsed platform command line arguments.

        Returns:
            Resolved input path, or `None` on error.
        """
        # Resolve the input TOML path first
        input_path = Path(args.input).resolve()
        if not input_path.exists():
            print(f"Platform file not found: {args.input}")
            return None
        return input_path

    @classmethod
    def _resolve_platform(cls, toml_path: Path) -> PlatformResult:
        """Resolve one platform file into concrete copy entries.

        Args:
            toml_path: Resolved platform TOML file path.

        Returns:
            Final platform result.

        Raises:
            ValueError: The platform file is invalid.
        """
        # Load and validate the TOML content first
        data = cls._load_platform(toml_path)
        platform = data["platform"]
        directory = cls._resolve_directory(toml_path, data.get("dir"))
        entries_by_target: dict[str, CopyEntry] = {}

        # Expand add rules in order so later targets override earlier ones
        for item in platform.get("add", []):
            for entry in cls._resolve_add_rule(directory, item["src"],
                                               item["dst"]):
                entries_by_target[str(entry.target)] = entry

        return PlatformResult(
            name=platform["name"],
            directory=directory,
            entries=list(entries_by_target.values()),
        )

    @classmethod
    def _load_platform(cls, toml_path: Path) -> dict:
        """Load and validate one platform TOML file.

        Args:
            toml_path: Resolved platform TOML file path.

        Returns:
            Parsed TOML object.

        Raises:
            ValueError: The file is invalid or required fields are missing.
        """
        # Parse the TOML document first
        try:
            with toml_path.open("rb") as file:
                data = tomllib.load(file)
        except tomllib.TOMLDecodeError as exc:
            raise ValueError(f"Invalid TOML file: {toml_path} ({exc})") from exc

        # Validate the platform table
        platform = data.get("platform")
        if not isinstance(platform, dict):
            raise ValueError(f"Missing table: [platform] ({toml_path})")

        name = platform.get("name")
        if not isinstance(name, str) or not name.strip():
            raise ValueError(f"Missing field: platform.name ({toml_path})")

        # Validate the optional dir table
        directory = data.get("dir")
        if directory is not None:
            if not isinstance(directory, dict):
                raise ValueError(f"Invalid table: [dir] ({toml_path})")
            if not isinstance(directory.get("src"), str) or not isinstance(
                    directory.get("dst"), str):
                raise ValueError(f"Missing field: dir.src/dst ({toml_path})")

        # Validate the add rules
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
    def _resolve_directory(cls,
                           toml_path: Path,
                           directory: dict | None) -> PlatformDir:
        """Resolve the working directories for one platform file.

        Args:
            toml_path: Resolved platform TOML file path.
            directory: Optional `[dir]` table.

        Returns:
            Resolved source and output directories.
        """
        if directory is None:
            # Default to the TOML directory
            return PlatformDir(source=toml_path.parent, output=toml_path.parent)

        # Resolve both working directories from the TOML location
        return PlatformDir(
            source=cls._resolve_dir_path(toml_path, directory["src"],
                                         "source dir"),
            output=cls._resolve_dir_path(toml_path, directory["dst"],
                                         "output dir"),
        )

    @classmethod
    def _resolve_dir_path(cls,
                          toml_path: Path,
                          value: str,
                          description: str) -> Path:
        """Resolve one directory path from the input TOML file.

        Args:
            toml_path: TOML file that owns this directory definition.
            value: Raw directory path string.
            description: Human-readable path description.

        Returns:
            Resolved absolute directory path.

        Raises:
            ValueError: The directory path is invalid.
        """
        # Allow parent traversal here because [dir] is TOML-relative
        normalized = cls._normalize_relative_path(
            value,
            description,
            allow_dot=True,
            allow_parent=True,
        )
        return (toml_path.parent / normalized).resolve()

    @classmethod
    def _resolve_add_rule(cls,
                          directory: PlatformDir,
                          src: str,
                          dst: str) -> list[CopyEntry]:
        """Resolve one add rule into concrete copy entries.

        Args:
            directory: Resolved source and output directories.
            src: Source path text.
            dst: Destination path text.

        Returns:
            Concrete copy entries for the rule.

        Raises:
            ValueError: The rule is invalid.
        """
        # Normalize both rule paths first
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

        # Reject multi-source to single-file rules
        if len(sources.files) > 1 and cls._looks_like_file_path(target_root):
            raise ValueError(
                f"Invalid destination path for multi-source rule: {target_root}")

        entries: list[CopyEntry] = []
        for source_path, relative_path in sources.files:
            target_path = target_root if not sources.keep_structure else (
                    target_root / relative_path).resolve()
            entries.append(CopyEntry(source=source_path, target=target_path))
        return entries

    @classmethod
    def _resolve_sources(cls, source_root: Path, src: str) -> SourceSet:
        """Resolve one source path into concrete files.

        Args:
            source_root: Resolved source directory.
            src: Source path or pattern.

        Returns:
            Resolved source files and layout mode.

        Raises:
            ValueError: The source path is invalid or missing.
        """
        if cls._has_wildcard(src):
            # Wildcards keep relative layout
            return SourceSet(files=cls._resolve_glob_sources(source_root, src),
                             keep_structure=True)

        source_path = (source_root / src).resolve()
        if not source_path.exists():
            raise ValueError(f"Source path not found: {src}")
        if source_path.is_dir():
            # Directories keep relative layout
            return SourceSet(files=cls._walk_directory(source_path),
                             keep_structure=True)

        # Single files map directly to dst
        return SourceSet(files=[(source_path, Path())], keep_structure=False)

    @classmethod
    def _resolve_glob_sources(cls,
                              source_root: Path,
                              pattern: str) -> list[tuple[Path, Path]]:
        """Resolve one wildcard pattern into concrete files.

        Args:
            source_root: Resolved source directory.
            pattern: Source pattern relative to the source directory.

        Returns:
            Source files and their relative paths.

        Raises:
            ValueError: The source pattern matched nothing.
        """
        # Expand the wildcard pattern first
        matches = sorted(source_root.glob(pattern))
        if not matches:
            raise ValueError(f"Source path not found: {pattern}")

        anchor = source_root / cls._glob_anchor(pattern)
        files: list[tuple[Path, Path]] = []
        for match in matches:
            if match.is_dir():
                # Expand matched directories into files
                for child, relative_path in cls._walk_directory(match):
                    files.append(
                        (child, match.relative_to(anchor) / relative_path))
                continue
            files.append((match.resolve(), match.relative_to(anchor)))
        return files

    @staticmethod
    def _walk_directory(source_path: Path) -> list[tuple[Path, Path]]:
        """Collect all files under one source directory.

        Args:
            source_path: Resolved source directory path.

        Returns:
            Source files and their relative paths.
        """
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
            # Stop at the first wildcard segment
            if any(char in part for char in "*?["):
                break
            parts.append(part)
        return Path(*parts) if parts else Path(".")

    @staticmethod
    def _looks_like_file_path(path: Path) -> bool:
        """Return whether the destination path looks like a single file path."""
        return path.name not in ("", ".") and path.suffix != ""

    @staticmethod
    def _normalize_relative_path(value: str,
                                 description: str,
                                 allow_dot: bool = False,
                                 allow_parent: bool = False) -> str:
        """Normalize and validate one relative path string.

        Args:
            value: Raw path string.
            description: Human-readable path description.
            allow_dot: Whether `.` is accepted as a valid relative path.
            allow_parent: Whether `..` is accepted in the path.

        Returns:
            Normalized relative path using `/` separators.

        Raises:
            ValueError: The path is invalid.
        """
        # Normalize path separators first
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
    def _format_display_path(path: Path) -> str:
        """Return a concise path string for user-facing CLI output.

        Args:
            path: Resolved path to display.

        Returns:
            Path relative to the current working directory when possible.
        """
        try:
            display_path = path.resolve().relative_to(Path.cwd().resolve())
        except ValueError:
            display_path = path
        return display_path.as_posix()

    @staticmethod
    def _print_plan(result: PlatformResult) -> None:
        """Print the resolved platform assembly plan.

        Args:
            result: Final platform result.
        """
        # Print the working directories first
        print(f"Platform: {result.name}")
        source_dir = PlatformCommand._format_display_path(
            result.directory.source)
        output_dir = PlatformCommand._format_display_path(
            result.directory.output)
        print(f"Source dir: {source_dir}")
        print(f"Output dir: {output_dir}")
        for entry in result.entries:
            source = PlatformCommand._format_display_path(entry.source)
            target = PlatformCommand._format_display_path(entry.target)
            print(f"Copy: {source} -> {target}")

    @staticmethod
    def _clean_output(output_root: Path, manifest_path: Path) -> None:
        """Remove files managed by the previous platform generation.

        Args:
            output_root: Resolved platform output directory.
            manifest_path: Resolved manifest file path.
        """
        if not manifest_path.exists():
            return

        # Delete all previously recorded files first
        for line in manifest_path.read_text(encoding="utf-8").splitlines():
            target_text = line.strip()
            if not target_text:
                continue
            target_path = Path(target_text)
            if target_path.exists():
                target_path.unlink()
                PlatformCommand._clean_empty_parents(output_root,
                                                     target_path.parent)

        # Remove the old manifest last
        manifest_path.unlink(missing_ok=True)

    @staticmethod
    def _clean_empty_parents(output_root: Path, path: Path) -> None:
        """Remove empty parent directories under the output directory.

        Args:
            output_root: Directory where cleanup should stop.
            path: First directory to check.
        """
        current = path
        # Stop at the output root or a non-empty directory
        while current != output_root and current.is_relative_to(output_root):
            try:
                current.rmdir()
            except OSError:
                break
            current = current.parent

    @staticmethod
    def _copy_files(entries: list[CopyEntry]) -> list[str]:
        """Copy all resolved platform files into the output directory.

        Args:
            entries: Resolved copy entries.

        Raises:
            ValueError: A resolved source path does not exist.

        Returns:
            Output file paths written in this run.
        """
        copied_files: list[str] = []
        for entry in entries:
            copied_files.append(PlatformCommand._copy_one_file(entry))
        return copied_files

    @staticmethod
    def _copy_one_file(entry: CopyEntry) -> str:
        """Copy one resolved source file to its target path.

        Args:
            entry: One resolved copy entry.

        Returns:
            Written output file path.

        Raises:
            ValueError: The source file no longer exists.
        """
        if not entry.source.exists():
            raise ValueError(f"Source path not found: {entry.source}")

        # Create the parent directory before copying
        entry.target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry.source, entry.target)
        source = PlatformCommand._format_display_path(entry.source)
        target = PlatformCommand._format_display_path(entry.target)
        print(f"Copy: {source} -> {target}")
        return str(entry.target)

    @staticmethod
    def _write_manifest(manifest_path: Path, copied_files: list[str]) -> None:
        """Write the platform manifest for the next clean step.

        Args:
            manifest_path: Resolved manifest file path.
            copied_files: Output file paths written in this run.
        """
        # Record generated files for the next cleanup
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        content = "\n".join(copied_files)
        if content:
            content += "\n"
        manifest_path.write_text(content, encoding="utf-8")
