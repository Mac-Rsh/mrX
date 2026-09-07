#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
MDK command - Add, delete, sync, or rename one uVision project.

Loads one `.uvprojx`/`.uvproj` file, then either updates target file entries
or renames the project file set.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace
from dataclasses import dataclass, field
from pathlib import Path
from tempfile import NamedTemporaryFile
from xml.etree.ElementTree import Element, ElementTree, ParseError
from xml.etree.ElementTree import SubElement, indent, parse
import os

from framework.command import Command, CommandResult, CommandSpec
from framework.completion import FILE_COMPLETION, PATH_COMPLETION
from framework.completion import attach_path_completion
from framework.context import CommandContext


@dataclass(slots=True, frozen=True)
class MDKPaths:
    """Resolved project and source paths for one command invocation."""

    project: Path
    source: Path | None
    source_text: str | None


@dataclass(slots=True)
class MDKGroupContext:
    """One target group and its file containers."""

    name: str
    element: Element
    files_elements: list[Element] = field(default_factory=list)


@dataclass(slots=True)
class MDKTargetContext:
    """Resolved target state used by the MDK command."""

    name: str
    element: Element
    groups_element: Element | None
    groups: dict[str, MDKGroupContext]


@dataclass(slots=True, frozen=True)
class MDKFileRecord:
    """One existing file entry inside the selected target."""

    group_name: str
    file_name: str
    file_type: int
    file_path: str
    path_key: str
    files_element: Element
    file_element: Element


@dataclass(slots=True, frozen=True)
class MDKAddEntry:
    """One file entry prepared for insertion into the project."""

    source: Path
    group_name: str
    file_name: str
    file_type: int
    file_path: str
    path_key: str


@dataclass(slots=True, frozen=True)
class MDKRenameEntry:
    """One file rename planned for the MDK project file set."""

    source: Path
    destination: Path


class MDKCommand(Command):
    """Built-in command for managing one MDK project file set."""

    DEFAULT_GROUP_NAME = "Sources"
    PROJECT_OPTION_SUFFIXES = {
        ".uvprojx": ".uvoptx",
        ".uvproj": ".uvopt",
    }
    PROJECT_USER_FILE_MARKERS = (
        ".uvguix.",
        ".uvgui.",
    )

    spec = CommandSpec(
        name="mdk",
        description="Add, delete, sync, or rename an MDK project",
    )

    def configure(self, parser: ArgumentParser) -> None:
        """Declare MDK command arguments."""
        parser.add_argument(
            "action",
            choices=("add", "del", "sync", "rename"),
            help="Operation to apply to the MDK project",
        )
        attach_path_completion(parser.add_argument(
            "-p",
            "--project",
            required=True,
            help="Target MDK '.uvprojx' or '.uvproj' file path",
        ), FILE_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-s",
            "--source",
            help="File or directory path used by add/del/sync mode",
        ), PATH_COMPLETION)
        parser.add_argument(
            "-n",
            "--name",
            help="New project name used by rename mode",
        )
        parser.add_argument(
            "-t",
            "--target",
            help=("Target name inside the MDK project (used by add/del/sync, "
                  "and rename for multi-target projects)"),
        )
        parser.add_argument(
            "-g",
            "--group",
            help=("Root MDK group name used by add/del/sync mode; omit it "
                  "for directory add, del, or sync to enable auto grouping"),
        )
        parser.add_argument(
            "--keep-empty-groups",
            action="store_true",
            help="Keep empty groups after del or sync removes files",
        )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Execute the MDK project update workflow."""
        paths = self._parse_paths(context, arguments)
        if isinstance(paths, str):
            return CommandResult.failure(paths)

        try:
            if arguments.action == "rename":
                renamed_project = self._rename_project(paths.project,
                                                       arguments.name,
                                                       arguments.target)
                context.console.write(f"Build: {renamed_project}")
                return CommandResult.success()

            updated_project = self._update_project(paths, arguments, context)
            context.console.write(f"Build: {updated_project}")
            return CommandResult.success()
        except ValueError as exc:
            return CommandResult.failure(str(exc))

    @classmethod
    def _update_project(cls, paths: MDKPaths, arguments: Namespace,
                        context: CommandContext) -> Path:
        """Apply add, del, or sync mode updates to one MDK project file."""
        if paths.source is None or paths.source_text is None:
            raise ValueError(
                f"Missing source path for `{arguments.action}` mode")

        # Load the selected project target and current file list
        tree = cls._load_project(paths.project)
        target = cls._select_target(tree.getroot(), arguments.target)
        existing_files = cls._collect_existing_files(target,
                                                     paths.project.parent)
        project_updated = False

        # Apply the requested in-memory update before writing the project
        if arguments.action in {"add", "sync"}:
            entries = cls._collect_requested_add_entries(
                paths,
                arguments.group,
                paths.project.parent,
                allow_empty=(arguments.action == "sync"),
            )
            entries, skipped_messages = cls._filter_add_entries(entries,
                                                                existing_files)
            for message in skipped_messages:
                context.console.warning(f"Skip: {message}")

            if arguments.action == "sync":
                missing_files = cls._collect_sync_remove_files(
                    paths, existing_files, paths.project.parent)
                for record in missing_files:
                    context.console.write(
                        "Remove: missing source file in group "
                        f"`{record.group_name}`: {record.file_path}"
                    )
                if missing_files:
                    cls._remove_files(target, missing_files,
                                      arguments.keep_empty_groups)
                    project_updated = True

            if entries:
                cls._add_entries(target, entries)
                project_updated = True
        else:
            matched_files, skipped_messages = (
                cls._collect_requested_remove_files(
                    paths,
                    arguments.group,
                    existing_files,
                    paths.project.parent,
                )
            )
            for message in skipped_messages:
                context.console.warning(f"Skip: {message}")
            if matched_files:
                cls._remove_files(target, matched_files,
                                  arguments.keep_empty_groups)
                project_updated = True

        # Write the updated project only after all checks succeed
        if project_updated:
            cls._write_project(tree, paths.project)
        return paths.project

    @staticmethod
    def _parse_paths(context: CommandContext,
                     arguments: Namespace) -> MDKPaths | str:
        """Parse and validate command input paths."""
        project_path = context.resolve_path(arguments.project)

        if not project_path.exists():
            return f"MDK project file not found: {arguments.project}"
        suffix = project_path.suffix.lower()
        if suffix not in {".uvprojx", ".uvproj"}:
            return f"Unsupported MDK project file: {arguments.project}"

        if arguments.action in {"add", "del", "sync"}:
            if not arguments.source:
                return f"`--source` is required for `{arguments.action}` mode"
            source_path = context.resolve_path(arguments.source)
            source_text = arguments.source
        else:
            source_path = None
            source_text = None

        if arguments.action == "rename" and not (arguments.name or "").strip():
            return "`--name` is required for `rename` mode"

        return MDKPaths(
            project=project_path,
            source=source_path,
            source_text=source_text,
        )

    @classmethod
    def _rename_project(cls,
                        project_path: Path,
                        raw_name: str | None,
                        target_name: str | None) -> Path:
        """Rename one MDK project file set and return the new project path."""
        new_name = cls._normalize_project_name(project_path, raw_name)
        rename_entries = cls._build_rename_entries(project_path, new_name)
        staged_updates = cls._stage_rename_updates(project_path, target_name,
                                                   new_name, rename_entries)

        try:
            cls._apply_rename_entries(rename_entries)
        except ValueError:
            cls._cleanup_staged_updates(staged_updates)
            raise

        try:
            cls._install_staged_updates(staged_updates)
        except ValueError as exc:
            try:
                cls._rollback_rename_entries(rename_entries)
            except ValueError as rollback_exc:
                raise ValueError(
                    f"{exc}\nRollback failed: {rollback_exc}") from exc
            raise

        return rename_entries[0].destination

    @classmethod
    def _stage_rename_updates(
            cls,
            project_path: Path,
            target_name: str | None,
            new_name: str,
            rename_entries: list[MDKRenameEntry],
    ) -> list[tuple[Path, Path]]:
        """Prepare updated XML files before any project file is renamed."""
        project_tree = cls._load_project(project_path)
        old_target_name = cls._select_target(project_tree.getroot(),
                                             target_name).name
        cls._check_target_rename_conflict(project_tree.getroot(),
                                          old_target_name, new_name)
        cls._update_project_target(project_tree, old_target_name, new_name)
        staged_updates = [
            (cls._stage_xml_tree(project_tree, rename_entries[0].destination),
             rename_entries[0].destination)
        ]
        option_suffix = cls.PROJECT_OPTION_SUFFIXES.get(
            project_path.suffix.lower())

        if option_suffix is None:
            return staged_updates
        option_path = project_path.with_suffix(option_suffix)
        if not option_path.exists():
            return staged_updates

        option_tree = cls._load_option_file(option_path)
        cls._update_option_target(option_tree, old_target_name, new_name)
        staged_updates.append(
            (cls._stage_xml_tree(option_tree,
                                 rename_entries[0].destination.with_suffix(
                                     option_suffix)),
             rename_entries[0].destination.with_suffix(option_suffix)))
        return staged_updates

    @classmethod
    def _check_target_rename_conflict(cls, root: Element,
                                      old_target_name: str,
                                      new_name: str) -> None:
        """Reject rename targets that would collide with another MDK target."""
        for target_element in root.findall("./Targets/Target"):
            target_name = (target_element.findtext("TargetName") or "").strip()
            if target_name == new_name and target_name != old_target_name:
                raise ValueError(
                    f"Rename target conflicts with existing target: {new_name}")

    @classmethod
    def _update_project_target(cls, tree: ElementTree, old_target_name: str,
                               new_name: str) -> None:
        """Update one in-memory project tree for rename mode."""
        target = cls._select_target(tree.getroot(), old_target_name)
        target_name_element = target.element.find("TargetName")
        output_name_element = target.element.find(
            "./TargetOption/TargetCommonOption/OutputName")

        if target_name_element is not None:
            target_name_element.text = new_name
        if output_name_element is not None:
            output_name_element.text = new_name

    @classmethod
    def _update_option_target(cls, tree: ElementTree, old_target_name: str,
                              new_name: str) -> None:
        """Update one in-memory option tree for rename mode."""
        target_element = cls._select_option_target(tree.getroot(),
                                                   old_target_name)
        target_name_element = target_element.find("TargetName")

        if target_name_element is not None:
            target_name_element.text = new_name

    @staticmethod
    def _create_temp_path(reference_path: Path, suffix_text: str) -> Path:
        """Create one temporary path beside the reference file."""
        try:
            with NamedTemporaryFile(delete=False,
                                    dir=reference_path.parent,
                                    prefix=f".{reference_path.name}.",
                                    suffix=suffix_text) as handle:
                return Path(handle.name)
        except OSError as exc:
            raise ValueError(
                "Failed to create temporary file near: "
                f"{reference_path} ({exc})"
            ) from exc

    @classmethod
    def _stage_xml_tree(cls, tree: ElementTree, destination_path: Path) -> Path:
        """Write one updated XML tree to a temporary file."""
        staged_path = cls._create_temp_path(destination_path, ".tmp")

        try:
            cls._write_xml_file(tree, staged_path)
        except ValueError:
            try:
                staged_path.unlink()
            except OSError:
                pass
            raise
        return staged_path

    @staticmethod
    def _cleanup_staged_updates(
            staged_updates: list[tuple[Path, Path]]) -> None:
        """Remove any temporary XML files that are still present."""
        for staged_path, _ in staged_updates:
            if not staged_path.exists():
                continue
            try:
                staged_path.unlink()
            except OSError:
                continue

    @classmethod
    def _install_staged_updates(cls,
                                staged_updates: list[
                                    tuple[Path, Path]]) -> None:
        """Atomically replace renamed XML files with their updated content."""
        backups: list[tuple[Path, Path]] = []

        try:
            for staged_path, destination_path in staged_updates:
                backup_path = cls._create_temp_path(destination_path, ".bak")
                os.replace(destination_path, backup_path)
                backups.append((backup_path, destination_path))
                os.replace(staged_path, destination_path)
        except (OSError, ValueError) as exc:
            for backup_path, destination_path in reversed(backups):
                try:
                    os.replace(backup_path, destination_path)
                except OSError:
                    continue
            raise ValueError(
                f"Failed to update renamed MDK project files: {exc}") from exc
        finally:
            cls._cleanup_staged_updates(staged_updates)

        for backup_path, _ in backups:
            try:
                backup_path.unlink()
            except OSError:
                continue

    @classmethod
    def _normalize_project_name(cls, project_path: Path,
                                raw_name: str | None) -> str:
        """Validate and normalize one rename target name."""
        name_text = (raw_name or "").strip()
        project_suffix = project_path.suffix.lower()

        if not name_text:
            raise ValueError("Missing rename target: --name")

        name_path = Path(name_text)
        if name_path.name != name_text or name_text in {".", ".."}:
            raise ValueError(f"Invalid project name: {raw_name}")

        if name_path.suffix:
            if name_path.suffix.lower() != project_suffix:
                raise ValueError(
                    "Rename target must keep project suffix "
                    f"`{project_suffix}`: {raw_name}"
                )
            normalized_name = name_path.stem.strip()
        else:
            normalized_name = name_text

        if not normalized_name:
            raise ValueError(f"Invalid project name: {raw_name}")
        if normalized_name == project_path.stem:
            raise ValueError(
                "New project name is the same as the current name: "
                f"{normalized_name}"
            )
        return normalized_name

    @classmethod
    def _build_rename_entries(cls, project_path: Path,
                              new_name: str) -> list[MDKRenameEntry]:
        """Build and validate the file rename plan for one project."""
        rename_entries = [
            MDKRenameEntry(
                source=project_path,
                destination=project_path.with_name(
                    f"{new_name}{project_path.suffix}"),
            )
        ]
        option_suffix = cls.PROJECT_OPTION_SUFFIXES.get(
            project_path.suffix.lower())

        if option_suffix is not None:
            option_path = project_path.with_suffix(option_suffix)
            if option_path.exists():
                rename_entries.append(
                    MDKRenameEntry(
                        source=option_path,
                        destination=option_path.with_name(
                            f"{new_name}{option_suffix}"),
                    ))

        rename_entries.extend(cls._collect_user_file_renames(project_path,
                                                             new_name))
        cls._validate_rename_entries(rename_entries)
        return rename_entries

    @classmethod
    def _collect_user_file_renames(
            cls,
            project_path: Path,
            new_name: str,
    ) -> list[MDKRenameEntry]:
        """Collect matching Keil user files that should follow the rename."""
        rename_entries: list[MDKRenameEntry] = []
        project_name = project_path.stem

        for marker in cls.PROJECT_USER_FILE_MARKERS:
            pattern = f"{project_name}{marker}*"
            for source_path in sorted(project_path.parent.glob(pattern)):
                suffix_text = source_path.name[len(project_name):]
                rename_entries.append(
                    MDKRenameEntry(
                        source=source_path,
                        destination=source_path.with_name(
                            f"{new_name}{suffix_text}"),
                    ))
        return rename_entries

    @classmethod
    def _validate_rename_entries(
            cls,
            rename_entries: list[MDKRenameEntry],
    ) -> None:
        """Validate that every rename source and destination is safe."""
        destination_map: dict[str, Path] = {}

        for entry in rename_entries:
            if not entry.source.exists():
                raise ValueError(f"Rename source not found: {entry.source}")

            source_key = cls._path_key(entry.source)
            destination_key = cls._path_key(entry.destination)
            owner = destination_map.get(destination_key)
            if owner is not None:
                raise ValueError(
                    "Duplicate rename target detected: "
                    f"{entry.destination} and {owner}"
                )
            destination_map[destination_key] = entry.source

            if entry.destination.exists() and destination_key != source_key:
                raise ValueError(
                    f"Rename target already exists: {entry.destination}")

    @classmethod
    def _apply_rename_entries(cls,
                              rename_entries: list[MDKRenameEntry]) -> None:
        """Apply one validated rename plan and roll back on failure."""
        applied_entries: list[MDKRenameEntry] = []

        try:
            for entry in rename_entries:
                cls._rename_path(entry.source, entry.destination)
                applied_entries.append(entry)
        except OSError as exc:
            cls._rollback_rename_entries(applied_entries)
            raise ValueError(
                f"Failed to rename MDK project files: {exc}") from exc

    @classmethod
    def _rollback_rename_entries(cls,
                                 rename_entries: list[MDKRenameEntry]) -> None:
        """Restore one applied rename plan back to its original paths."""
        rollback_errors: list[str] = []

        for entry in reversed(rename_entries):
            if not entry.destination.exists():
                continue
            try:
                cls._rename_path(entry.destination, entry.source)
            except OSError as exc:
                rollback_errors.append(
                    f"{entry.destination} -> {entry.source} ({exc})")

        if rollback_errors:
            raise ValueError("; ".join(rollback_errors))

    @classmethod
    def _rename_path(cls, source_path: Path, destination_path: Path) -> None:
        """Rename one path, including Windows case-only name changes."""
        if cls._path_key(source_path) == cls._path_key(destination_path):
            if source_path.name == destination_path.name:
                return
            temporary_path = cls._create_temp_path(source_path, ".rename")
            temporary_path.unlink()
            os.replace(source_path, temporary_path)
            os.replace(temporary_path, destination_path)
            return
        os.replace(source_path, destination_path)

    @staticmethod
    def _load_option_file(option_path: Path) -> ElementTree:
        """Load one MDK option file."""
        try:
            return parse(option_path)
        except ParseError as exc:
            raise ValueError(
                f"Invalid MDK option file: {option_path} ({exc})") from exc
        except OSError as exc:
            raise ValueError(
                f"Failed to read MDK option file: {option_path} ({exc})"
            ) from exc

    @staticmethod
    def _select_option_target(root: Element, target_name: str) -> Element:
        """Resolve one target element from an MDK option file."""
        target_elements = root.findall("./Target")
        target_names: list[str] = []

        if not target_elements:
            raise ValueError("Missing target list: /ProjectOpt/Target")

        for element in target_elements:
            target_names.append((element.findtext("TargetName") or "").strip())

        target_element = next(
            (element for element in target_elements
             if (element.findtext("TargetName") or "").strip() == target_name),
            None,
        )
        if target_element is None:
            available = ", ".join(
                name or "<unnamed>" for name in target_names)
            raise ValueError(
                f"Target not found in option file: {target_name} "
                f"(available: {available})"
            )
        return target_element

    @staticmethod
    def _load_project(project_path: Path) -> ElementTree:
        """Load one MDK project file.

        Arguments:
            project_path: Resolved MDK project path.

        Returns:
            Parsed XML tree.

        Raises:
            ValueError: The project file cannot be parsed.
        """
        try:
            return parse(project_path)
        except ParseError as exc:
            raise ValueError(
                f"Invalid MDK project file: {project_path} ({exc})") from exc
        except OSError as exc:
            raise ValueError(
                f"Failed to read MDK project file: {project_path} ({exc})"
            ) from exc

    @classmethod
    def _select_target(cls, root: Element,
                       target_name: str | None) -> MDKTargetContext:
        """Resolve the target that will be updated.

        Arguments:
            root: MDK project XML root element.
            target_name: Optional target name from the command line.

        Returns:
            Resolved target context.

        Raises:
            ValueError: The project has no valid target selection.
        """
        target_elements = root.findall("./Targets/Target")
        target_names: list[str] = []

        if not target_elements:
            raise ValueError("Missing target list: /Project/Targets/Target")

        # Collect target names first so selection errors can report choices
        for element in target_elements:
            target_names.append((element.findtext("TargetName") or "").strip())

        # Resolve the selected target element
        if target_name is None:
            if len(target_elements) != 1:
                raise ValueError(
                    "Multiple targets found, please specify `--target`: " +
                    ", ".join(name or "<unnamed>" for name in target_names))
            target_element = target_elements[0]
        else:
            target_element = next(
                (element for element in target_elements
                 if (element.findtext("TargetName") or "").strip()
                 == target_name),
                None,
            )
            if target_element is None:
                available = ", ".join(
                    name or "<unnamed>" for name in target_names)
                raise ValueError(
                    f"Target not found: {target_name} "
                    f"(available: {available})"
                )

        # Collect group state for the selected target
        groups_element = target_element.find("Groups")
        return MDKTargetContext(
            name=(target_element.findtext("TargetName") or "").strip(),
            element=target_element,
            groups_element=groups_element,
            groups=cls._collect_groups(groups_element),
        )

    @staticmethod
    def _collect_groups(groups_element: Element | None
                        ) -> dict[str, MDKGroupContext]:
        """Collect target groups and validate group names.

        Arguments:
            groups_element: Target `Groups` XML element.

        Returns:
            Mapping of group name to group context.

        Raises:
            ValueError: Group names are missing or duplicated.
        """
        groups: dict[str, MDKGroupContext] = {}

        if groups_element is None:
            return groups

        # Validate and collect all group containers in document order
        for element in groups_element.findall("./Group"):
            group_name = (element.findtext("GroupName") or "").strip()
            if not group_name:
                raise ValueError("Invalid MDK group: missing GroupName")
            if group_name in groups:
                raise ValueError(
                    "Duplicate MDK group name detected in target: "
                    f"{group_name}")
            groups[group_name] = MDKGroupContext(
                name=group_name,
                element=element,
                files_elements=element.findall("./Files"),
            )
        return groups

    @classmethod
    def _collect_existing_files(
            cls,
            target: MDKTargetContext,
            project_dir: Path,
    ) -> list[MDKFileRecord]:
        """Collect all existing file records from the selected target.

        Arguments:
            target: Selected target context.
            project_dir: MDK project parent directory.

        Returns:
            Existing file records in document order.

        Raises:
            ValueError: One file entry is structurally invalid.
        """
        records: list[MDKFileRecord] = []

        # Walk every group and capture the current file entries
        for group in target.groups.values():
            for files_element in group.files_elements:
                for file_element in files_element.findall("./File"):
                    file_name = (
                            file_element.findtext("FileName") or "").strip()
                    file_path = (
                            file_element.findtext("FilePath") or "").strip()
                    file_type = cls._parse_file_type(file_element)
                    if not file_name or not file_path:
                        raise ValueError(
                            "Invalid MDK file entry in group "
                            f"`{group.name}`: missing FileName or FilePath"
                        )

                    # Resolve one stable path key for duplicate detection
                    path_key = cls._path_key(
                        cls._resolve_project_path(project_dir, file_path))
                    records.append(
                        MDKFileRecord(
                            group_name=group.name,
                            file_name=file_name,
                            file_type=file_type,
                            file_path=file_path,
                            path_key=path_key,
                            files_element=files_element,
                            file_element=file_element,
                        ))
        return records

    @staticmethod
    def _parse_file_type(file_element: Element) -> int:
        """Parse one MDK file type value.

        Arguments:
            file_element: MDK `File` XML element.

        Returns:
            Parsed file type.
        """
        text = (file_element.findtext("FileType") or "5").strip()

        try:
            return int(text)
        except ValueError:
            return 5

    @classmethod
    def _collect_add_entries(
            cls,
            source_path: Path,
            group_name: str | None,
            project_dir: Path,
            allow_empty: bool = False,
    ) -> list[MDKAddEntry]:
        """Collect file entries that should be inserted into the project.

        Arguments:
            source_path: Source file or directory path.
            group_name: Optional root MDK group name.
            project_dir: MDK project parent directory.

        Returns:
            File entries prepared for insertion.

        Raises:
            ValueError: The source path is invalid.
        """
        entries: list[MDKAddEntry] = []
        resolved_source = source_path.resolve()
        root_group_name = cls._normalize_group_name(group_name)

        if not resolved_source.exists():
            raise ValueError(f"Source path not found: {source_path}")
        if cls._has_hidden_name(resolved_source.name):
            raise ValueError(
                "Hidden source paths are not supported by MDK add: "
                f"{source_path}"
            )

        # Resolve the source file list and the default root group name
        if resolved_source.is_file():
            files = [resolved_source]
            if not root_group_name:
                root_group_name = cls._normalize_group_name(
                    resolved_source.parent.name) or cls.DEFAULT_GROUP_NAME
        else:
            files = sorted(path.resolve() for path in resolved_source.rglob("*")
                           if
                           path.is_file() and not cls._has_hidden_relative_path(
                               resolved_source, path))
            if not files:
                if allow_empty:
                    return []
                raise ValueError(f"Source directory is empty: {source_path}")
            if not root_group_name:
                root_group_name = cls._normalize_group_name(
                    resolved_source.name)
        if not root_group_name:
            root_group_name = cls.DEFAULT_GROUP_NAME

        # Build the final MDK file entries in one pass
        for resolved_path in files:
            relative_dir = cls._relative_group_dir(resolved_source,
                                                   resolved_path)
            target_group_name = root_group_name
            if relative_dir:
                target_group_name = cls._normalize_group_name(
                    f"{root_group_name}/{relative_dir}")
            file_name = resolved_path.name
            file_type = cls._detect_file_type(resolved_path)
            file_path = cls._project_path_text(project_dir, resolved_path)
            entries.append(
                MDKAddEntry(
                    source=resolved_path,
                    group_name=target_group_name,
                    file_name=file_name,
                    file_type=file_type,
                    file_path=file_path,
                    path_key=cls._path_key(resolved_path),
                ))
        return entries

    @classmethod
    def _collect_auto_add_entries(
            cls,
            source_path: Path,
            group_name: str | None,
            project_dir: Path,
            allow_empty: bool = False,
    ) -> list[MDKAddEntry]:
        """Collect file entries by expanding visible first-level directories."""
        resolved_source = source_path.resolve()
        root_group_name = cls._normalize_group_name(group_name)

        if not resolved_source.exists():
            raise ValueError(f"Source path not found: {source_path}")
        if not resolved_source.is_dir():
            raise ValueError(
                "Automatic group expansion requires a directory source "
                f"path: {source_path}"
            )
        if cls._has_hidden_name(resolved_source.name):
            raise ValueError(
                "Hidden source paths are not supported by automatic "
                f"group expansion: {source_path}"
            )

        entries: list[MDKAddEntry] = []
        child_directories = sorted(
            path.resolve() for path in resolved_source.iterdir()
            if path.is_dir() and not cls._has_hidden_name(
                path.name))

        if not child_directories:
            if allow_empty:
                return []
            raise ValueError(
                "No visible child directories found for automatic group "
                f"expansion: {source_path}"
            )

        for child_path in child_directories:
            if not cls._contains_visible_files(child_path):
                continue
            child_group_name = child_path.name
            if root_group_name:
                child_group_name = cls._normalize_group_name(
                    f"{root_group_name}/{child_group_name}")
            entries.extend(
                cls._collect_add_entries(child_path, child_group_name,
                                         project_dir, allow_empty=allow_empty))

        if not entries:
            if allow_empty:
                return []
            raise ValueError(
                "No visible files found below the automatic group "
                f"expansion source path: {source_path}"
            )
        return entries

    @classmethod
    def _collect_requested_add_entries(
            cls,
            paths: MDKPaths,
            group_name: str | None,
            project_dir: Path,
            allow_empty: bool = False,
    ) -> list[MDKAddEntry]:
        """Collect add entries for one source request."""
        if paths.source is None:
            return []

        if allow_empty and not paths.source.exists():
            return []

        if not group_name and paths.source.exists() and paths.source.is_dir():
            return cls._collect_auto_add_entries(paths.source,
                                                 group_name,
                                                 project_dir,
                                                 allow_empty=allow_empty)
        return cls._collect_add_entries(paths.source,
                                        group_name,
                                        project_dir,
                                        allow_empty=allow_empty)

    @classmethod
    def _collect_requested_remove_files(
            cls,
            paths: MDKPaths,
            group_name: str | None,
            existing_files: list[MDKFileRecord],
            project_dir: Path,
    ) -> tuple[list[MDKFileRecord], list[str]]:
        """Collect del-mode file entries and skipped path messages."""
        if paths.source is None:
            return [], []

        if paths.source.exists():
            entries = cls._collect_requested_add_entries(paths,
                                                         group_name,
                                                         project_dir,
                                                         allow_empty=True)
            return cls._match_remove_entries(entries, existing_files)
        return cls._match_remove_files(paths, existing_files)

    @staticmethod
    def _normalize_group_name(group_name: str | None) -> str:
        """Normalize one MDK group name string.

        Arguments:
            group_name: Raw group name text.

        Returns:
            Normalized group name.
        """
        normalized = (group_name or "").replace("\\", "/").strip(" /")
        return normalized

    @staticmethod
    def _has_hidden_name(name: str) -> bool:
        """Check whether one path name uses a dot-prefixed hidden entry."""
        return name.startswith(".") and name not in {".", ".."}

    @classmethod
    def _has_hidden_relative_path(cls, source_root: Path,
                                  file_path: Path) -> bool:
        """Check whether one source file sits below any hidden path segment."""
        relative_path = file_path.relative_to(source_root)
        return any(cls._has_hidden_name(part) for part in relative_path.parts)

    @classmethod
    def _contains_visible_files(cls, source_root: Path) -> bool:
        """Check whether one directory contains any visible descendant file."""
        return any(path.is_file() and not cls._has_hidden_relative_path(
            source_root, path) for path in source_root.rglob("*"))

    @classmethod
    def _collect_sync_remove_files(
            cls,
            paths: MDKPaths,
            existing_files: list[MDKFileRecord],
            project_dir: Path,
    ) -> list[MDKFileRecord]:
        """Collect project file entries that no longer exist on disk."""
        if paths.source is None:
            return []

        scope_files = cls._collect_sync_scope_files(paths, existing_files)
        missing_files: list[MDKFileRecord] = []

        for record in scope_files:
            resolved_path = cls._resolve_project_path(project_dir,
                                                      record.file_path)
            if resolved_path.exists():
                continue
            missing_files.append(record)
        return missing_files

    @classmethod
    def _collect_sync_scope_files(
            cls,
            paths: MDKPaths,
            existing_files: list[MDKFileRecord],
    ) -> list[MDKFileRecord]:
        """Collect existing project file entries covered by one sync request."""
        descendants: list[MDKFileRecord] = []
        exact_matches: list[MDKFileRecord] = []
        path_key = cls._path_key(paths.source)
        prefix = path_key + os.sep

        for record in existing_files:
            if record.path_key == path_key:
                exact_matches.append(record)
                continue
            if record.path_key.startswith(prefix):
                descendants.append(record)

        if cls._has_directory_intent(paths):
            return descendants
        if exact_matches:
            return exact_matches
        if descendants:
            return descendants
        return []

    @staticmethod
    def _relative_group_dir(source_root: Path, file_path: Path) -> str:
        """Build the relative directory portion used for group naming."""
        if source_root.is_file():
            return ""
        relative_path = file_path.relative_to(source_root)
        relative_dir = relative_path.parent
        if str(relative_dir) == ".":
            return ""
        return relative_dir.as_posix()

    @staticmethod
    def _project_path_text(project_dir: Path, file_path: Path) -> str:
        """Build the MDK `FilePath` string for one source file.

        Arguments:
            project_dir: MDK project parent directory.
            file_path: Resolved source file path.

        Returns:
            Relative path when possible, otherwise absolute path.
        """
        try:
            relative_path = os.path.relpath(file_path, project_dir)
        except ValueError:
            relative_path = str(file_path)
        return relative_path.replace("/", "\\")

    @classmethod
    def _detect_file_type(cls, source_path: Path) -> int:
        """Map one file suffix to an MDK `FileType` value."""
        suffix = source_path.suffix.lower()

        if suffix == ".c":
            return 1
        if suffix in {".cc", ".cpp", ".cxx"}:
            return 8
        if suffix in {".s", ".sx", ".asm"}:
            return 2
        if suffix in {".o", ".obj"}:
            return 3
        if suffix in {".a", ".lib"}:
            return 4
        return 5

    @staticmethod
    def _path_key(path: Path) -> str:
        """Normalize one path into a stable conflict-detection key."""
        return os.path.normcase(
            os.path.normpath(str(path.resolve(strict=False))))

    @staticmethod
    def _resolve_project_path(project_dir: Path, file_path: str) -> Path:
        """Resolve one MDK `FilePath` string against the project directory."""
        path = Path(file_path)

        if path.is_absolute():
            return path.resolve(strict=False)
        return (project_dir / path).resolve(strict=False)

    @classmethod
    def _filter_add_entries(
            cls,
            entries: list[MDKAddEntry],
            existing_files: list[MDKFileRecord],
    ) -> tuple[list[MDKAddEntry], list[str]]:
        """Filter duplicate add entries and return skipped path messages.

        Arguments:
            entries: Prepared add entries.
            existing_files: Existing target file records.

        Returns:
            Remaining entries to add and warning messages for skipped paths.
        """
        filtered_entries: list[MDKAddEntry] = []
        skipped_messages: list[str] = []
        existing_by_path: dict[str, MDKFileRecord] = {}
        new_path_owner: dict[str, MDKAddEntry] = {}

        # Index existing entries by path only; MDK can manage same-stem files.
        for existing_record in existing_files:
            existing_by_path[existing_record.path_key] = existing_record

        # Keep the first owner for each path and skip later duplicates.
        for owner in entries:
            existing_record = existing_by_path.get(owner.path_key)
            if existing_record is not None:
                skipped_messages.append(
                    "Path already exists in group "
                    f"`{existing_record.group_name}`: "
                    f"{existing_record.file_path}"
                )
                continue

            if owner.path_key in new_path_owner:
                skipped_messages.append(
                    "Duplicate input path detected: "
                    f"{owner.source} and "
                    f"{new_path_owner[owner.path_key].source}"
                )
                continue

            new_path_owner[owner.path_key] = owner
            filtered_entries.append(owner)
        return filtered_entries, skipped_messages

    @classmethod
    def _match_remove_entries(
            cls,
            entries: list[MDKAddEntry],
            existing_files: list[MDKFileRecord],
    ) -> tuple[list[MDKFileRecord], list[str]]:
        """Match del entries by path and keep unmatched paths as warnings."""
        matched_files: list[MDKFileRecord] = []
        skipped_messages: list[str] = []
        existing_by_path: dict[str, MDKFileRecord] = {}
        seen_paths: dict[str, Path] = {}

        for existing_record in existing_files:
            existing_by_path[existing_record.path_key] = existing_record

        for entry in entries:
            previous_owner = seen_paths.get(entry.path_key)
            if previous_owner is not None:
                skipped_messages.append(
                    "Duplicate input path detected: "
                    f"{entry.source} and {previous_owner}"
                )
                continue
            seen_paths[entry.path_key] = entry.source

            existing_record = existing_by_path.get(entry.path_key)
            if existing_record is None:
                skipped_messages.append(
                    f"Path not found in project: {entry.file_path}"
                )
                continue
            matched_files.append(existing_record)
        return matched_files, skipped_messages

    @classmethod
    def _add_entries(cls, target: MDKTargetContext,
                     entries: list[MDKAddEntry]) -> None:
        """Add prepared file entries into the selected target."""
        grouped_entries = cls._group_add_entries(entries)

        # Create missing groups and append files in sorted group order
        for group_name in sorted(grouped_entries):
            group, _ = cls._ensure_group(target, group_name)
            cls._append_group_files(group, grouped_entries[group_name])

    @staticmethod
    def _group_add_entries(
            entries: list[MDKAddEntry]) -> dict[str, list[MDKAddEntry]]:
        """Group add entries by their destination MDK group name."""
        grouped: dict[str, list[MDKAddEntry]] = {}
        sorted_entries = sorted(entries,
                                key=lambda item: (item.group_name,
                                                  item.file_path))

        for entry in sorted_entries:
            grouped.setdefault(entry.group_name, []).append(entry)
        return grouped

    @classmethod
    def _ensure_group(cls, target: MDKTargetContext,
                      group_name: str) -> tuple[MDKGroupContext, bool]:
        """Ensure the selected target contains one specific group."""
        group = target.groups.get(group_name)

        if group is not None:
            return group, False
        if target.groups_element is None:
            target.groups_element = SubElement(target.element, "Groups")

        # Create the missing group and register it in the target context
        group_element = SubElement(target.groups_element, "Group")
        name_element = SubElement(group_element, "GroupName")
        name_element.text = group_name
        group = MDKGroupContext(name=group_name, element=group_element)
        target.groups[group_name] = group
        return group, True

    @classmethod
    def _append_group_files(cls, group: MDKGroupContext,
                            entries: list[MDKAddEntry]) -> None:
        """Append new file entries into one MDK group."""
        use_split_style = cls._use_split_files_style(group.files_elements)

        # Preserve whether this group uses one shared or split `Files` layout
        for entry in entries:
            if use_split_style or not group.files_elements:
                files_element = SubElement(group.element, "Files")
                group.files_elements.append(files_element)
            else:
                files_element = group.files_elements[-1]

            file_element = SubElement(files_element, "File")
            file_name = SubElement(file_element, "FileName")
            file_type = SubElement(file_element, "FileType")
            file_path = SubElement(file_element, "FilePath")

            file_name.text = entry.file_name
            file_type.text = str(entry.file_type)
            file_path.text = entry.file_path

    @staticmethod
    def _use_split_files_style(files_elements: list[Element]) -> bool:
        """Check whether a group stores one file per `Files` container."""
        if len(files_elements) <= 1:
            return False
        return all(
            len(element.findall("./File")) == 1 for element in files_elements)

    @classmethod
    def _match_remove_files(
            cls,
            paths: MDKPaths,
            existing_files: list[MDKFileRecord],
    ) -> tuple[list[MDKFileRecord], list[str]]:
        """Match target file entries for one del request.

        Arguments:
            paths: Resolved project and source paths.
            existing_files: Existing target file records.

        Returns:
            Matched file records and skipped path messages.
        """
        descendants: list[MDKFileRecord] = []
        exact_matches: list[MDKFileRecord] = []
        is_directory = cls._has_directory_intent(paths)
        path_key = cls._path_key(paths.source)
        prefix = path_key + os.sep

        # Match exact file entries and directory descendants separately
        for record in existing_files:
            if record.path_key == path_key:
                exact_matches.append(record)
                continue
            if record.path_key.startswith(prefix):
                descendants.append(record)

        if is_directory:
            if not descendants:
                return [], [
                    f"No MDK files matched directory path: {paths.source_text}"
                ]
            return descendants, []
        if exact_matches:
            return exact_matches, []
        if descendants:
            return descendants, []
        return [], [f"No MDK files matched path: {paths.source_text}"]

    @staticmethod
    def _has_directory_intent(paths: MDKPaths) -> bool:
        """Return whether del or sync explicitly targets a directory."""
        if paths.source is None:
            return False
        if paths.source.exists():
            return paths.source.is_dir()
        return (paths.source_text or "").endswith(("/", "\\"))

    @classmethod
    def _remove_files(
            cls,
            target: MDKTargetContext,
            matched_files: list[MDKFileRecord],
            keep_empty_groups: bool,
    ) -> int:
        """Remove matched file entries and optionally prune empty groups."""
        emptied_files_elements: list[tuple[str, Element]] = []
        group_names: set[str] = set()
        removed_groups = 0

        # Remove matched `File` entries first and collect emptied `Files` blocks
        for record in matched_files:
            record.files_element.remove(record.file_element)
            if not record.files_element.findall("./File"):
                emptied_files_elements.append((record.group_name,
                                               record.files_element))
            group_names.add(record.group_name)

        # Remove empty `Files` containers before deciding whether groups are empty
        for group_name, files_element in emptied_files_elements:
            group = target.groups.get(group_name)
            if group is None:
                continue
            if files_element in list(group.element):
                group.element.remove(files_element)

        if keep_empty_groups or target.groups_element is None:
            return 0

        # Remove groups that no longer contain any file entries
        for group_name in list(group_names):
            group = target.groups.get(group_name)
            if group is None:
                continue
            group.files_elements = group.element.findall("./Files")
            if group.element.findall("./Files/File"):
                continue
            target.groups_element.remove(group.element)
            del target.groups[group_name]
            removed_groups += 1
        return removed_groups

    @staticmethod
    def _write_xml_file(tree: ElementTree, xml_path: Path) -> None:
        """Write one updated XML file back to disk.

        Arguments:
            tree: Updated XML tree.
            xml_path: Destination XML file path.

        Raises:
            ValueError: The XML file cannot be written.
        """
        try:
            indent(tree, space="  ")
            tree.write(xml_path, encoding="utf-8", xml_declaration=True)
        except OSError as exc:
            raise ValueError(
                f"Failed to write XML file: {xml_path} ({exc})") from exc

    @classmethod
    def _write_project(cls, tree: ElementTree, project_path: Path) -> None:
        """Write one updated MDK project file back to disk.

        Arguments:
            tree: Updated XML tree.
            project_path: Destination MDK project path.

        Raises:
            ValueError: The project file cannot be written.
        """
        cls._write_xml_file(tree, project_path)
