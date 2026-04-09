#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Sections command - Insert managed linker section collection rules.

Loads one TOML section description file, groups entries by output section, then
updates the managed blocks inside one GNU ld style linker script.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace
from dock import PluginSpec, export_plugin
from dataclasses import dataclass
from pathlib import Path
import tomllib
import re


@dataclass(slots=True, frozen=True)
class SectionEntry:
    """One validated linker section rule entry."""

    name: str
    input: str
    output: str
    keep: bool
    sort: bool
    comment: str = ""


@dataclass(slots=True, frozen=True)
class BlockSpan:
    """Resolved brace block span inside the linker script text."""

    start: int
    open_brace: int
    close_brace: int


@dataclass(slots=True, frozen=True)
class SectionPaths:
    """Resolved config and linker script paths."""

    config: Path
    input: Path


@export_plugin
class SectionsCommand:
    """Built-in command for updating linker section collection rules."""

    DEFAULT_CONFIG_PATH = "sections.toml"
    INDENT = "    "
    TAG_PREFIX = "@mrX: "
    SECTIONS_NAME = "SECTIONS"
    SECTIONS_ERROR = f"Failed to find `{SECTIONS_NAME} {{ ... }}` in linker script"

    spec = PluginSpec(
        name="sections",
        description="Insert managed linker section rules.",
    )

    def help(self) -> str:
        """Return detailed help text for the sections command.

        Returns:
            Human-readable help text for the command.
        """
        parser = self._build_parser()
        lines = [
            "Usage: python ./scripts/cli.py sections -i <input.ld> [options]",
            "Description: Insert managed linker section rules into one `.ld` file.",
            "Aliases: (none)",
            "",
            parser.format_help().rstrip(),
            f"Default config: {self.DEFAULT_CONFIG_PATH}",
        ]
        return "\n".join(lines)

    def run(self, args: list[str]) -> int:
        """Execute the linker section update workflow.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Parse command line arguments
        options = self._parse_args(args)
        if options is None:
            return 1

        # Resolve the config and linker script paths
        paths = self._parse_paths(options)
        if paths is None:
            return 1
        # Load all configured section entries first
        try:
            sections = self._load_sections(paths.config)
        except ValueError as exc:
            print(exc)
            return 1

        # Read the target linker script before applying any updates
        linker_text = self._read_text(paths.input, "linker script")
        if linker_text is None:
            return 1

        # Apply all managed block updates in memory first
        try:
            updated_text = self._update_linker_script(linker_text, sections)
        except ValueError as exc:
            print(exc)
            return 1

        # Write the updated linker script back to disk only on success
        if not self._write_text(paths.input, updated_text, "linker script"):
            return 1

        print(f"Build: {paths.input}")
        return 0

    @classmethod
    def _build_parser(cls) -> ArgumentParser:
        """Build the sections command line argument parser.

        Returns:
            Configured argument parser for the sections command.
        """
        parser = ArgumentParser(
            prog="python ./scripts/cli.py sections",
            add_help=True,
        )
        parser.add_argument(
            "-i",
            "--input",
            required=True,
            help="Target linker script '.ld' file path",
        )
        parser.add_argument(
            "-c",
            "--config",
            default=cls.DEFAULT_CONFIG_PATH,
            help=f"Section TOML file path (default: {cls.DEFAULT_CONFIG_PATH})",
        )
        return parser

    @classmethod
    def _parse_args(cls, args: list[str]) -> Namespace | None:
        """Parse sections command line arguments.

        Args:
            args: Raw command line arguments passed to the command.

        Returns:
            Parsed argument namespace, or `None` when parsing fails.
        """
        parser = cls._build_parser()

        try:
            return parser.parse_args(args)
        except SystemExit:
            return None

    @staticmethod
    def _parse_paths(args: Namespace) -> SectionPaths | None:
        """Parse and validate command input paths.

        Args:
            args: Parsed sections command line arguments.

        Returns:
            Resolved config and linker script paths, or `None` on error.
        """
        # Resolve the section config path first
        config_path = Path(args.config).resolve()
        if not config_path.exists():
            print(f"Section config file not found: {args.config}")
            return None

        # Resolve the linker script input path next
        input_path = Path(args.input).resolve()
        if not input_path.exists():
            print(f"Linker script file not found: {args.input}")
            return None

        return SectionPaths(config=config_path, input=input_path)

    @staticmethod
    def _read_text(path: Path, description: str) -> str | None:
        """Read one UTF-8 text file with a consistent error message.

        Args:
            path: Target file path.
            description: Human-readable file description.

        Returns:
            File text on success, or `None` when reading fails.
        """
        try:
            return path.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"Failed to read {description}: {path} ({exc})")
            return None

    @staticmethod
    def _write_text(path: Path, text: str, description: str) -> bool:
        """Write one UTF-8 text file with a consistent error message.

        Args:
            path: Target file path.
            text: File content to write.
            description: Human-readable file description.

        Returns:
            `True` when writing succeeds, else `False`.
        """
        try:
            path.write_text(text, encoding="utf-8")
        except OSError as exc:
            print(f"Failed to write {description}: {path} ({exc})")
            return False
        return True

    @classmethod
    def _load_sections(cls, config_path: Path) -> list[SectionEntry]:
        """Load and validate one section TOML file.

        Args:
            config_path: Resolved TOML file path.

        Returns:
            Validated section entries in file order.

        Raises:
            ValueError: The TOML file is invalid.
        """
        # Parse the TOML document first
        try:
            with config_path.open("rb") as file:
                data = tomllib.load(file)
        except tomllib.TOMLDecodeError as exc:
            raise ValueError(
                f"Invalid section TOML file: {config_path} ({exc})") from exc

        # Validate the top-level section array
        section_items = data.get("section")
        if not isinstance(section_items, list):
            raise ValueError(f"Missing table: [[section]] ({config_path})")

        sections: list[SectionEntry] = []
        for index, item in enumerate(section_items, start=1):
            sections.append(cls._build_section_entry(config_path, index, item))

        if not sections:
            raise ValueError(f"Empty section config: {config_path}")

        return sections

    @staticmethod
    def _build_section_entry(config_path: Path, index: int,
                             item: object) -> SectionEntry:
        """Validate one `[[section]]` entry.

        Args:
            config_path: Resolved TOML file path.
            index: One-based entry index.
            item: Raw TOML value.

        Returns:
            Validated section entry.

        Raises:
            ValueError: The entry is invalid.
        """
        if not isinstance(item, dict):
            raise ValueError(
                f"Invalid [[section]] entry #{index}: {config_path}")

        name = SectionsCommand._require_text_field(config_path, index, item,
                                                   "name")
        input_name = SectionsCommand._require_text_field(config_path, index,
                                                         item, "input")
        output = SectionsCommand._require_text_field(config_path, index, item,
                                                     "output")
        keep = SectionsCommand._require_bool_field(config_path, index, item,
                                                   "keep")
        sort = SectionsCommand._require_bool_field(config_path, index, item,
                                                   "sort")
        comment = SectionsCommand._optional_text_field(config_path, index, item,
                                                       "comment")

        return SectionEntry(
            name=name,
            input=input_name,
            output=output,
            keep=keep,
            sort=sort,
            comment=comment,
        )

    @staticmethod
    def _require_text_field(config_path: Path, index: int, item: dict,
                            key: str) -> str:
        """Read one required non-empty string field from a section entry."""
        value = item.get(key)
        if not isinstance(value, str) or not value.strip():
            raise ValueError(
                f"Missing field: section[{index}].{key} ({config_path})")
        return value.strip()

    @staticmethod
    def _optional_text_field(config_path: Path, index: int, item: dict,
                             key: str) -> str:
        """Read one optional string field from a section entry."""
        value = item.get(key, "")
        if not isinstance(value, str):
            raise ValueError(
                f"Invalid field: section[{index}].{key} ({config_path})")
        return value.strip()

    @staticmethod
    def _require_bool_field(config_path: Path, index: int, item: dict,
                            key: str) -> bool:
        """Read one required boolean field from a section entry."""
        value = item.get(key)
        if not isinstance(value, bool):
            raise ValueError(
                f"Invalid field: section[{index}].{key} ({config_path})")
        return value

    @classmethod
    def _update_linker_script(cls, linker_text: str,
                              sections: list[SectionEntry]) -> str:
        """Update the managed blocks inside one linker script text.

        Args:
            linker_text: Original linker script text.
            sections: Validated section entries.

        Returns:
            Updated linker script text.

        Raises:
            ValueError: The linker script structure is not supported.
        """
        # Group all input rules by their destination output section first
        grouped_sections = cls._group_sections(sections)
        updated_text = linker_text

        # Update one output section at a time
        for output, items in grouped_sections.items():
            updated_text = cls._upsert_output_block(updated_text, output, items)

        return updated_text

    @staticmethod
    def _group_sections(
            sections: list[SectionEntry]) -> dict[str, list[SectionEntry]]:
        """Group section entries by output section while preserving order."""
        grouped: dict[str, list[SectionEntry]] = {}

        # Preserve file order inside each output section group
        for item in sections:
            grouped.setdefault(item.output, []).append(item)
        return grouped

    @classmethod
    def _upsert_output_block(cls, linker_text: str, output: str,
                             items: list[SectionEntry]) -> str:
        """Insert or replace one managed block for the requested output.

        Args:
            linker_text: Current linker script text.
            output: Output section name to update.
            items: Section entries assigned to the output section.

        Returns:
            Updated linker script text.

        Raises:
            ValueError: `SECTIONS` was not found or the script is malformed.
        """
        sections_span = cls._find_sections_block(linker_text)
        if sections_span is None:
            raise ValueError(cls.SECTIONS_ERROR)

        # Remove any previous managed marker block for this output first
        linker_text = cls._remove_managed_block(linker_text, output)
        sections_span = cls._find_sections_block(linker_text)
        if sections_span is None:
            raise ValueError(cls.SECTIONS_ERROR)

        # Update an existing output section when it already exists
        output_span = cls._find_output_block(linker_text, sections_span, output)
        if output_span is not None:
            return cls._insert_into_output_block(linker_text, output_span,
                                                 output,
                                                 items)

        # Create a new output section block inside `SECTIONS`
        return cls._append_output_block(linker_text, sections_span, output,
                                        items)

    @classmethod
    def _insert_into_output_block(cls, linker_text: str, output_span: BlockSpan,
                                  output: str,
                                  items: list[SectionEntry]) -> str:
        """Insert the managed block into an existing output section."""
        # Reuse the output section indentation so generated text follows style
        section_indent = cls._line_indent(linker_text, output_span.start)
        body_indent = section_indent + cls.INDENT
        managed_block = cls._render_managed_block(output, items, body_indent)
        body_start = output_span.open_brace + 1
        body_end = output_span.close_brace
        body = linker_text[body_start:body_end]

        # Remove previously generated tagged entries before rewriting them
        body = cls._remove_generated_entries(body, items, body_indent)

        # Keep user content untouched and append the managed block at the end
        new_body = cls._append_managed_block(body, managed_block)

        return linker_text[:body_start] + new_body + linker_text[body_end:]

    @classmethod
    def _append_output_block(cls, linker_text: str, sections_span: BlockSpan,
                             output: str, items: list[SectionEntry]) -> str:
        """Append a new output section block before the closing `SECTIONS` brace."""
        # Indent the generated output section relative to the `SECTIONS` block
        sections_indent = cls._line_indent(linker_text, sections_span.start)
        output_indent = sections_indent + cls.INDENT
        body_indent = output_indent + cls.INDENT
        managed_block = cls._render_managed_block(output, items, body_indent)
        insert_at = cls._line_start(linker_text, sections_span.close_brace)
        prefix = linker_text[:insert_at]
        suffix = linker_text[insert_at:]

        # Build one complete output section block with the managed content only
        block = "\n".join((
            f"{output_indent}{output} :",
            f"{output_indent}{{",
            managed_block,
            f"{output_indent}}}",
        ))

        # Add one separating blank line unless this is the first `SECTIONS` item
        insertion = cls._build_output_insertion(prefix, block)

        return prefix + insertion + suffix

    @classmethod
    def _append_managed_block(cls, body: str, managed_block: str) -> str:
        """Append one managed block to an existing output section body."""
        nl = "\n"
        trailing = cls._trailing_whitespace(body)
        trailing_suffix = trailing if trailing else nl
        trimmed_body = body[:len(body) - len(trailing)] if trailing else body
        trimmed_body = trimmed_body.rstrip()

        # Start on a fresh line when the output section body was empty
        if not trimmed_body:
            return f"{nl}{managed_block}{trailing_suffix}"

        # Keep one blank line between user content and generated content
        return f"{trimmed_body}{nl}{nl}{managed_block}{trailing_suffix}"

    @staticmethod
    def _build_output_insertion(prefix: str, block: str) -> str:
        """Build the text inserted before the closing `SECTIONS` brace."""
        nl = "\n"
        if prefix.rstrip().endswith("{"):
            return f"{nl}{block}{nl}"
        return f"{nl}{nl}{block}{nl}"

    @staticmethod
    def _trailing_whitespace(text: str) -> str:
        """Return the trailing whitespace suffix of one text block."""
        index = len(text)

        # Walk backwards until the first non-whitespace character is found
        while index > 0 and text[index - 1] in (" ", "\t", "\r", "\n"):
            index -= 1
        return text[index:]

    @classmethod
    def _render_managed_block(cls, output: str, items: list[SectionEntry],
                              indent: str) -> str:
        """Render one managed block for one output section."""
        del output
        blocks: list[str] = []

        # Emit one tagged comment plus one rule per entry
        for item in items:
            blocks.append("\n".join((
                f"{indent}{cls._build_tag_comment(item)}",
                f"{indent}{cls._build_rule(item)}",
            )))

        # Keep generated entries compact and contiguous
        return "\n".join(blocks)

    @classmethod
    def _remove_generated_entries(cls, body: str, items: list[SectionEntry],
                                  indent: str) -> str:
        """Remove previously generated entries from one output body."""
        lines = body.splitlines()
        filtered: list[str] = []
        legacy_lines = cls._build_legacy_lines(items, indent)
        trailing = cls._trailing_whitespace(body)
        index = 0

        # Drop tagged entries and compatible legacy lines for the current items
        while index < len(lines):
            line = lines[index]

            if cls._is_tagged_comment(line, indent):
                index += 1

                # Consume blank spacer lines that may belong to older output.
                while index < len(lines) and lines[index].strip() == "":
                    index += 1

                # Consume the generated rule line when it exists.
                if index < len(lines) and cls._is_generated_rule(lines[index],
                                                                 indent):
                    index += 1

                # Consume one trailing blank separator after the generated entry.
                while index < len(lines) and lines[index].strip() == "":
                    index += 1
                continue
            if line in legacy_lines:
                index += 1
                continue

            filtered.append(line)
            index += 1

        # Trim trailing blank lines left after removing generated content
        while filtered and filtered[-1].strip() == "":
            filtered.pop()

        if not filtered:
            return ""

        # Preserve the original body suffix so the closing brace indentation stays intact
        return "\n".join(filtered) + trailing

    @classmethod
    def _build_tag_comment(cls, item: SectionEntry) -> str:
        """Build one tagged ownership comment for a generated section entry."""
        if item.comment:
            return f"/* {cls.TAG_PREFIX}{item.comment} */"
        return f"/* {cls.TAG_PREFIX}{item.name} */"

    @classmethod
    def _build_legacy_lines(cls, items: list[SectionEntry],
                            indent: str) -> set[str]:
        """Build legacy untagged lines compatible with older generated output."""
        lines: set[str] = set()

        # Keep compatibility with the old `/* comment */` plus rule format
        for item in items:
            if item.comment:
                lines.add(f"{indent}/* {item.comment} */")
            lines.add(f"{indent}{cls._build_rule(item)}")

        return lines

    @classmethod
    def _is_tagged_comment(cls, line: str, indent: str) -> bool:
        """Check whether one line is an owned `@mrX:` comment line."""
        stripped = cls._strip_indent(line, indent)
        return stripped.startswith("/* @mrX:") and stripped.endswith("*/")

    @classmethod
    def _is_generated_rule(cls, line: str, indent: str) -> bool:
        """Check whether one line looks like a generated linker rule line."""
        stripped = cls._strip_indent(line, indent)
        return stripped.startswith(("KEEP(", "*("))

    @staticmethod
    def _strip_indent(line: str, indent: str) -> str:
        """Strip one expected indentation prefix from a linker script line."""
        if line.startswith(indent):
            return line[len(indent):]
        return line.lstrip()

    @staticmethod
    def _build_rule(item: SectionEntry) -> str:
        """Build one linker collection rule from one section entry."""
        # Wrap the input section pattern with `SORT(...)` when requested
        if item.sort:
            rule = f"*(SORT({item.input}))"
        else:
            rule = f"*({item.input})"

        # Wrap the final rule with `KEEP(...)` when requested
        if item.keep:
            rule = f"KEEP({rule})"
        return rule

    @classmethod
    def _remove_managed_block(cls, linker_text: str, output: str) -> str:
        """Remove the managed block for one output section if it exists."""
        span = cls._find_managed_block(linker_text, output)
        if span is None:
            return linker_text
        return linker_text[:span[0]] + linker_text[span[1]:]

    @classmethod
    def _find_managed_block(cls, linker_text: str,
                            output: str) -> tuple[int, int] | None:
        """Find the full line span of one managed block."""
        begin_marker = f"/* mrX sections begin: {output} */"
        end_marker = f"/* mrX sections end: {output} */"

        # Locate the begin marker first to avoid deleting unrelated content
        begin = linker_text.find(begin_marker)
        if begin < 0:
            return None

        # Match the end marker from the located begin marker forward only
        end = linker_text.find(end_marker, begin)
        if end < 0:
            raise ValueError(f"Missing managed block end marker: {output}")

        # Expand to whole lines so surrounding formatting stays predictable
        block_start = cls._line_start(linker_text, begin)
        block_end = cls._line_end(linker_text, end)

        # Remove one extra trailing blank line when it belongs to this block
        if block_end < len(linker_text) and linker_text.startswith("\n",
                                                                   block_end):
            next_line_end = cls._line_end(linker_text, block_end)
            if linker_text[block_end:next_line_end].strip() == "":
                block_end = next_line_end

        return block_start, block_end

    @classmethod
    def _find_sections_block(cls, linker_text: str) -> BlockSpan | None:
        """Find the top-level `SECTIONS { ... }` block."""
        # Search the top-level `SECTIONS` keyword first
        word_boundary = r"\b"
        match = re.search(f"{word_boundary}{cls.SECTIONS_NAME}{word_boundary}", linker_text)
        if match is None:
            return None

        # Find the owning brace pair of the `SECTIONS` body
        open_brace = linker_text.find("{", match.end())
        if open_brace < 0:
            return None
        close_brace = cls._find_matching_brace(linker_text, open_brace)
        if close_brace is None:
            return None
        return BlockSpan(start=match.start(), open_brace=open_brace,
                         close_brace=close_brace)

    @classmethod
    def _find_output_block(cls, linker_text: str, sections_span: BlockSpan,
                           output: str) -> BlockSpan | None:
        """Find one output section block inside `SECTIONS`."""
        search_start = sections_span.open_brace + 1
        search_end = sections_span.close_brace
        pat_prefix = r"(?m)^[ \t]*"
        pat_suffix = r"(?=[ \t:(])"
        pattern = re.compile(f"{pat_prefix}{re.escape(output)}{pat_suffix}")

        # Scan every matching output section header candidate in order
        for match in pattern.finditer(linker_text, search_start, search_end):
            open_brace = cls._find_output_open_brace(linker_text, match.start(),
                                                     search_end)
            if open_brace is None:
                continue
            close_brace = cls._find_matching_brace(linker_text, open_brace)
            if close_brace is None or close_brace > search_end:
                continue
            return BlockSpan(start=match.start(), open_brace=open_brace,
                             close_brace=close_brace)
        return None

    @staticmethod
    def _find_output_open_brace(linker_text: str, start: int,
                                search_end: int) -> int | None:
        """Find the opening brace for one output section header."""
        colon_seen = False
        index = start

        # Walk the section header until `:` then the matching `{` appears
        while index < search_end:
            char = linker_text[index]
            if char == ":":
                colon_seen = True
            elif char == "{":
                return index if colon_seen else None
            elif char == ";":
                return None
            index += 1

        return None

    @staticmethod
    def _find_matching_brace(text: str, open_brace: int) -> int | None:
        """Find the matching closing brace for one opening brace."""
        depth = 0
        index = open_brace
        in_block_comment = False
        in_line_comment = False
        in_string = False

        # Skip braces inside comments and strings while tracking nesting depth
        while index < len(text):
            char = text[index]
            next_char = text[index + 1] if index + 1 < len(text) else ""

            if in_block_comment:
                if char == "*" and next_char == "/":
                    in_block_comment = False
                    index += 2
                    continue
                index += 1
                continue

            if in_line_comment:
                if char == "\n":
                    in_line_comment = False
                index += 1
                continue

            if in_string:
                if char == "\\":
                    index += 2
                    continue
                if char == '"':
                    in_string = False
                index += 1
                continue

            if char == "/" and next_char == "*":
                in_block_comment = True
                index += 2
                continue
            if char == "/" and next_char == "/":
                in_line_comment = True
                index += 2
                continue
            if char == '"':
                in_string = True
                index += 1
                continue
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return index

            index += 1

        return None

    @staticmethod
    def _line_indent(text: str, index: int) -> str:
        """Return the leading indentation for the line containing `index`."""
        line_start = SectionsCommand._line_start(text, index)
        line_end = text.find("\n", line_start)
        if line_end < 0:
            line_end = len(text)

        indent = []

        # Collect only leading spaces and tabs from the current line
        for char in text[line_start:line_end]:
            if char not in (" ", "\t"):
                break
            indent.append(char)
        return "".join(indent)

    @staticmethod
    def _line_start(text: str, index: int) -> int:
        """Return the start offset of the line containing `index`."""
        line_start = text.rfind("\n", 0, index)
        return 0 if line_start < 0 else line_start + 1

    @staticmethod
    def _line_end(text: str, index: int) -> int:
        """Return the end offset of the line containing `index`."""
        line_end = text.find("\n", index)
        return len(text) if line_end < 0 else line_end + 1
