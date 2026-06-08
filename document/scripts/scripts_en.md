[**中文**](scripts_scripts.md) | [**English**](scripts_scripts_en.md)

---

# Scripts Tools

The Scripts module provides the Python command-line tools for the mrX project. It is used to generate configuration headers, compile hardware tree files, install platform files, maintain linker section rules, and synchronize MDK project files.

The command entry point is `scripts/cli.py`, and plugin discovery and registration are implemented in `scripts/dock.py`. Run the commands from the `mrX` directory when possible, because most default input and output paths are resolved relative to the current working directory.

## Basic Usage

### Install Dependencies

```text
python -m pip install -r scripts/requirements.txt
```

`requirements.txt` contains `kconfiglib` and `windows-curses`. The scripts also use the standard-library `tomllib` module, so Python 3.11 or newer is recommended.

### Command Entry

```text
python ./scripts/cli.py <command> [args...]
```

Common helper commands:

```text
python ./scripts/cli.py list
python ./scripts/cli.py help
python ./scripts/cli.py help kconfig
```

`dock.py` is the plugin framework, not a standalone command-line entry. Normal usage should go through `cli.py`.

## Plugin Mechanism

Plugins are exported with `@export_plugin` and provide `PluginSpec` plus `run(args)`:

```python
@export_plugin
class ExampleCommand:
    spec = PluginSpec(name="example", description="Example command.")

    def run(self, args: list[str]) -> int:
        return 0
```

`Dock.discover_plugins()` scans top-level modules under `scripts/plugins` and direct `.py` modules in first-level subpackages. Plugins may register aliases through `spec.aliases`; name or alias conflicts are reported and the first registered plugin is kept.

Available commands:

| Command | Purpose |
| --- | --- |
| `list` | List discovered primary commands |
| `help` | Show command usage |
| `echo` | Print the passed arguments |
| `kconfig` | Process Kconfig and generate a C configuration header |
| `htc` | Compile `.hts`/`.htsi` hardware tree files into `mr_hardgen.h` |
| `sections` | Insert managed section rules into a GNU ld linker script |
| `platform` | Install, uninstall, or inspect platform files |
| `mdk` | Manage Keil MDK/uVision project files |

## Configuration Generation

### Kconfig

```text
python ./scripts/cli.py kconfig [options]
```

The `kconfig` command processes `Kconfig` and `.config`, then generates a C header. By default, it starts the external `menuconfig` command before resolving the input and output paths from options; pass `--no-ui` when only file generation is needed. Default paths:

| Option | Default | Description |
| --- | --- | --- |
| `-k, --kconfig` | `Kconfig` | Kconfig input file |
| `-c, --config` | `.config` | Kconfig configuration file |
| `-o, --output` | `./include/mrX/mr_config.h` | Output C header |
| `--no-ui` | disabled | Do not start `menuconfig` |

Examples:

```text
python ./scripts/cli.py kconfig --no-ui
python ./scripts/cli.py kconfig -k Kconfig -c .config -o include/mrX/mr_config.h --no-ui
```

The generated header removes the `CONFIG_` prefix. For example, `CONFIG_MR_USE_LOG` becomes `MR_USE_LOG`. This command rewrites `.config` and the output header; automated builds should usually pass `--no-ui`.

### HTC Hardware Tree Compiler

```text
python ./scripts/cli.py htc -i <input.hts|input.htsi> [-o <output.h>]
```

The `htc` command compiles `.hts` or `.htsi` hardware tree source into a C macro header. The default output is `./include/mrX/mr_hardgen.h`. The command creates the output directory and overwrites the target header. The generated macros are consumed by the `MR_HT_*` macros in `mr_hardtree.h` to access nodes, properties, labels, aliases, and compatible instances.

`.hts` is usually the main hardware tree input file. `.htsi` is usually a reusable include fragment, equivalent to the relationship between Linux Devicetree `.dts` and `.dtsi` files. The preprocessor reads both `.hts` and `.htsi` files through include paths.

Compilation pipeline:

```text
Preprocessor -> Lexer -> Parser -> SemanticBuilder -> TreeValidator -> MacroBuilder -> header
```

An `.hts`/`.htsi` file must use the root-node form:

```dts
/ {
    aliases {
        console = &uart0;
    }

    uart0: uart@0x4000 {
        compatible = "uart";
        status = "okay";
        baudrate = 115200;
    }
}
```

The main supported syntax includes node labels, node addresses, normal properties, flag properties, label references, path references, expanded arrays `<...>`, and inline arrays `[...]`.

Supported preprocessor directives:

```c
/include/"file.htsi"
#include "file.htsi"
#include <file.htsi>
#define NAME value
#undef NAME
#if expr
#ifdef NAME
#ifndef NAME
#elif expr
#else
#endif
```

Node `status` supports `okay`, `disabled`, `early`, `last`, `defer`, and `prio(n)`, where `n` must be in `0..9`. `disabled` nodes do not produce normal node macros, and references to disabled nodes emit warnings.

## Linker Section Rules

### Sections

```text
python ./scripts/cli.py sections -i <input.ld> [options]
```

The `sections` command reads `sections.toml` and inserts configured input section rules into the `SECTIONS { ... }` block of a GNU ld linker script.

| Option | Default | Description |
| --- | --- | --- |
| `-i, --input` | none | Target linker script, required |
| `-c, --config` | `sections.toml` | Section configuration file |

Configuration example:

```toml
[[section]]
name = "initcall"
input = "mr_initcall.*"
output = ".text"
keep = true
sort = true
comment = "Initcall"
```

Generated rules are marked with `@mrX:` comments:

```c
/* @mrX: Initcall */
KEEP(*(SORT(mr_initcall.*)))
```

If the target output section already exists, the rule is inserted into that section. Otherwise, a new output section is created. The command rewrites the `.ld` file in place.

## Platform File Installation

### Platform

```text
python ./scripts/cli.py platform [install|uninstall|status] [options]
```

The `platform` command installs or uninstalls platform-specific files according to a platform `install.toml` file. Common usage:

```text
python ./scripts/cli.py platform install -i arch/win/install.toml --dry-run
python ./scripts/cli.py platform install -i arch/win/install.toml
python ./scripts/cli.py platform status -i arch/win/install.toml
python ./scripts/cli.py platform uninstall -i arch/win/install.toml
```

Options:

| Option | Description |
| --- | --- |
| `install` | Install platform files, the default action |
| `uninstall` | Restore the file state saved before install |
| `status` | Inspect platform installation state |
| `-i, --input` | Platform `install.toml` |
| `--dry-run` | Print the plan without writing files |
| `--force` | Ignore changed installed files during uninstall |

Platform configuration example:

```toml
[platform]
name = "win"

[dir]
src = "."
dst = "./../.."

[[platform.add]]
src = "include/platform"
dst = "include/mrX/platform"
```

Install writes `.platform/manifest.json` and `.platform/previous`, which record file state and support uninstall restoration. If an installed target file changed after installation, uninstall refuses to restore it by default; use `--force` only when those changes can be discarded.

## MDK Project Maintenance

### MDK

```text
python ./scripts/cli.py mdk <add|del|sync|rename> -p <project.uvprojx> [options]
```

The `mdk` command manages Keil MDK/uVision project files. Both `.uvprojx` and `.uvproj` are supported.

| Option | Description |
| --- | --- |
| `add` | Add source files or directories to the project |
| `del` | Remove matching file entries from the project |
| `sync` | Add new files and remove missing project entries within the `--source` scope |
| `rename` | Rename the project file set and project output name |
| `-p, --project` | Target `.uvprojx` or `.uvproj` file |
| `-s, --source` | Source file or directory for `add`, `del`, and `sync` |
| `-n, --name` | New project name for `rename` |
| `-t, --target` | Target name in a multi-target project |
| `-g, --group` | MDK Group name |
| `--keep-empty-groups` | Keep empty groups after removal |

Examples:

```text
python ./scripts/cli.py mdk add -p project.uvprojx -s src
python ./scripts/cli.py mdk sync -p project.uvprojx -s src -g Sources
python ./scripts/cli.py mdk rename -p old.uvprojx -n new
```

`add`, `del`, and `sync` modify the project XML. `rename` may also rename `.uvoptx`, `.uvopt`, `.uvguix.*`, or `.uvgui.*` files, and updates `TargetName` and `OutputName` in the project.

When `--source` is a directory and `--group` is not specified, `add`, `del`, and `sync` automatically group visible first-level subdirectories under the source directory and recurse inside each subtree. Hidden directories and files whose names start with a dot are skipped. When `--group` is specified, the source tree is kept below that group name, for example `Sources/subdir`.

## Notes

- Run scripts from the `mrX` directory to avoid resolving default relative paths to the wrong location.
- `kconfig` starts the external `menuconfig` command by default; pass `--no-ui` if it is not installed.
- `kconfig` requires an existing `.config`, and rewrites both `.config` and the output header.
- `htc` creates the output directory and overwrites the header specified by `-o` or the default `mr_hardgen.h`.
- `htc` implements mrX's custom hardware tree syntax; it is not a complete Devicetree DTS implementation.
- `htc` includes forbid absolute paths and `..` traversal, and macros are limited to simple object-like macros.
- `sections` requires a recognizable `SECTIONS { ... }` block and rewrites the input file in place.
- `sections` removes and regenerates managed rules marked with `/* @mrX: ... */`; do not format hand-written content as generated blocks.
- `platform install` and `uninstall` copy, delete, or restore files; run with `--dry-run` first when possible.
- `platform uninstall --force` ignores changes made after install and should be used carefully.
- `mdk rename` renames several project-related files and updates XML; save or commit project files before running it.
- If a plugin import fails, the error is printed and that plugin is skipped. Use `list` to confirm the commands currently available.
