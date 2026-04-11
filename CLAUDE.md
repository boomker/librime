# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

librime is a modular, extensible Chinese input method engine written in C++17. It powers the Rime input method framework across platforms (macOS/Squirrel, Windows/Weasel, Linux/ibus-rime, Android/Trime, iOS/Hamster). The library processes key events through a configurable pipeline to produce Chinese character candidates.

## Build Commands

```bash
# Build third-party dependencies (git submodules under deps/)
make deps

# macOS: install Boost first
bash install-boost.sh && export BOOST_ROOT="$(pwd)/deps/boost-1.89.0"

# Build release
make                # or: make release

# Build debug (logs to stderr)
make debug

# Run tests
make test           # release build + test
make test-debug     # debug build + test

# Run tests directly after a build
cd build && ctest --output-on-failure

# Lint / format (Chromium style, clang-format 18)
make clang-format-lint
make clang-format-apply

# Build with merged plugins
make merged-plugins

# Build static library
make librime-static
```

Windows uses `build.bat` with subcommands: `deps`, `librime`, `test`, `clean`.

## Code Style

Chromium style via `.clang-format` (no sorted includes). Enforced in CI with clang-format-22. Run `make clang-format-apply` before committing. The lint/format scope covers `src/`, `test/`, `tools/`, `sample/`, and `plugins/` (excluding plugin submodule internals).

## Architecture

### Processing Pipeline

The engine (`ConcreteEngine` in `src/rime/engine.cc`) processes input through a sequential pipeline defined per-schema in YAML config under `engine/`:

1. **Processors** (`processor.h`) — Handle key events. Return `kAccepted` (consumed), `kRejected` (stop), or `kNoop` (pass to next). Examples: `speller`, `punctuator`, `selector`, `key_binder`.
2. **Segmentors** (`segmentor.h`) — Break input string into typed segments. Examples: `abc_segmentor`, `punct_segmentor`, `ascii_segmentor`.
3. **Translators** (`translator.h`) — Convert input segments into candidate translations. Examples: `script_translator`, `table_translator`, `punct_translator`.
4. **Filters** (`filter.h`) — Transform/reorder candidate lists. Examples: `simplifier` (OpenCC traditional→simplified), `uniquifier`, `charset_filter`.
5. **Formatters** (`formatter.h`) — Format final commit text. Example: `shape_formatter`.

### Component System

All pipeline stages are **components** registered in a global `Registry` singleton (`src/rime/registry.h`). The pattern:

- `Class<T, Arg>` (in `component.h`) defines a factory interface with `Create(Arg)` and `Require(name)` for lookup.
- `Component<T>` is a concrete factory that calls `new T(arg)`.
- Components are registered by name: `Registry::instance().Register("speller", new Component<Speller>)`.
- A `Ticket` bundles the engine pointer, component class name, and namespace for component creation.

### Module System

Modules group component registrations. Each module defines `rime_<name>_initialize()` / `rime_<name>_finalize()` functions and uses `RIME_REGISTER_MODULE(name)`. Key modules:

| Module | File | Components |
|--------|------|------------|
| `core` | `src/rime/core_module.cc` | Engine, schema, config system |
| `dict` | `src/rime/dict/dict_module.cc` | Dictionaries, prism, user DB |
| `gears` | `src/rime/gear/gears_module.cc` | All processors, segmentors, translators, filters |
| `levers` | `src/rime/lever/levers_module.cc` | Deployment, customization |

Module groups are defined in `src/rime/setup.cc`:
- `default` = core + dict + gears (for input)
- `deployer` = core + dict + levers + predict (for deployment)

### Plugin System

External plugins live in `plugins/<name>/` (cloned via `install-plugins.sh`). A plugin is a module that registers additional components. See `sample/` for the canonical example. Plugins are auto-discovered by `plugins/CMakeLists.txt` and can be built merged into the main library (`BUILD_MERGED_PLUGINS`) or loaded dynamically (`ENABLE_EXTERNAL_PLUGINS` via Boost.DLL).

### Key Source Layout

- `src/rime_api.h` / `src/rime_api.cc` — Public C API (the interface frontends consume)
- `src/rime/` — Core: engine, context, schema, service, deployer, key_event
- `src/rime/algo/` — Algorithms: syllabifier, spelling algebra, encoder, calculus
- `src/rime/config/` — YAML config system with compiler, plugins, and COW references
- `src/rime/dict/` — Dictionary subsystem: LevelDB, MARISA-trie prism, mapped files, user dict
- `src/rime/gear/` — All built-in pipeline components (~35 pairs of .cc/.h)
- `src/rime/lever/` — Deployment tasks, settings customization, user dict management
- `tools/` — CLI utilities: `rime_deployer`, `rime_console`, `rime_dict_manager`, `rime_patch`
- `test/` — Google Test unit tests

### Smart Pointer Aliases (common.h)

The codebase uses custom aliases throughout: `the<T>` = `unique_ptr`, `an<T>` / `of<T>` = `shared_ptr`, `weak<T>` = `weak_ptr`. Helper functions: `New<T>(...)` = `make_shared`, `As<X>(ptr)` = `dynamic_pointer_cast`, `Is<X>(ptr)` = type check.

### Dependencies

Built from git submodules via `deps.mk`: google-glog, googletest, leveldb, marisa-trie, opencc, yaml-cpp. Boost (1.89.0) is installed separately via `install-boost.sh`. Bundled headers in `include/`: darts.h (double-array trie), utf8.h.

## Tests

Tests use Google Test, built as a single `rime_test` binary. Test data YAML files are in `data/test/` and get copied to the build output directory. The test main (`test/rime_test_main.cc`) initializes the Rime API with test-specific traits before running.

<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **librime** (5331 symbols, 14120 relationships, 280 execution flows). Use the GitNexus MCP tools to understand code, assess impact, and navigate safely.

> If any GitNexus tool warns the index is stale, run `npx gitnexus analyze` in terminal first.

## Always Do

- **MUST run impact analysis before editing any symbol.** Before modifying a function, class, or method, run `gitnexus_impact({target: "symbolName", direction: "upstream"})` and report the blast radius (direct callers, affected processes, risk level) to the user.
- **MUST run `gitnexus_detect_changes()` before committing** to verify your changes only affect expected symbols and execution flows.
- **MUST warn the user** if impact analysis returns HIGH or CRITICAL risk before proceeding with edits.
- When exploring unfamiliar code, use `gitnexus_query({query: "concept"})` to find execution flows instead of grepping. It returns process-grouped results ranked by relevance.
- When you need full context on a specific symbol — callers, callees, which execution flows it participates in — use `gitnexus_context({name: "symbolName"})`.

## When Debugging

1. `gitnexus_query({query: "<error or symptom>"})` — find execution flows related to the issue
2. `gitnexus_context({name: "<suspect function>"})` — see all callers, callees, and process participation
3. `READ gitnexus://repo/librime/process/{processName}` — trace the full execution flow step by step
4. For regressions: `gitnexus_detect_changes({scope: "compare", base_ref: "main"})` — see what your branch changed

## When Refactoring

- **Renaming**: MUST use `gitnexus_rename({symbol_name: "old", new_name: "new", dry_run: true})` first. Review the preview — graph edits are safe, text_search edits need manual review. Then run with `dry_run: false`.
- **Extracting/Splitting**: MUST run `gitnexus_context({name: "target"})` to see all incoming/outgoing refs, then `gitnexus_impact({target: "target", direction: "upstream"})` to find all external callers before moving code.
- After any refactor: run `gitnexus_detect_changes({scope: "all"})` to verify only expected files changed.

## Never Do

- NEVER edit a function, class, or method without first running `gitnexus_impact` on it.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis.
- NEVER rename symbols with find-and-replace — use `gitnexus_rename` which understands the call graph.
- NEVER commit changes without running `gitnexus_detect_changes()` to check affected scope.

## Tools Quick Reference

| Tool | When to use | Command |
|------|-------------|---------|
| `query` | Find code by concept | `gitnexus_query({query: "auth validation"})` |
| `context` | 360-degree view of one symbol | `gitnexus_context({name: "validateUser"})` |
| `impact` | Blast radius before editing | `gitnexus_impact({target: "X", direction: "upstream"})` |
| `detect_changes` | Pre-commit scope check | `gitnexus_detect_changes({scope: "staged"})` |
| `rename` | Safe multi-file rename | `gitnexus_rename({symbol_name: "old", new_name: "new", dry_run: true})` |
| `cypher` | Custom graph queries | `gitnexus_cypher({query: "MATCH ..."})` |

## Impact Risk Levels

| Depth | Meaning | Action |
|-------|---------|--------|
| d=1 | WILL BREAK — direct callers/importers | MUST update these |
| d=2 | LIKELY AFFECTED — indirect deps | Should test |
| d=3 | MAY NEED TESTING — transitive | Test if critical path |

## Resources

| Resource | Use for |
|----------|---------|
| `gitnexus://repo/librime/context` | Codebase overview, check index freshness |
| `gitnexus://repo/librime/clusters` | All functional areas |
| `gitnexus://repo/librime/processes` | All execution flows |
| `gitnexus://repo/librime/process/{name}` | Step-by-step execution trace |

## Self-Check Before Finishing

Before completing any code modification task, verify:
1. `gitnexus_impact` was run for all modified symbols
2. No HIGH/CRITICAL risk warnings were ignored
3. `gitnexus_detect_changes()` confirms changes match expected scope
4. All d=1 (WILL BREAK) dependents were updated

## Keeping the Index Fresh

After committing code changes, the GitNexus index becomes stale. Re-run analyze to update it:

```bash
npx gitnexus analyze
```

If the index previously included embeddings, preserve them by adding `--embeddings`:

```bash
npx gitnexus analyze --embeddings
```

To check whether embeddings exist, inspect `.gitnexus/meta.json` — the `stats.embeddings` field shows the count (0 means no embeddings). **Running analyze without `--embeddings` will delete any previously generated embeddings.**

> Claude Code users: A PostToolUse hook handles this automatically after `git commit` and `git merge`.

## CLI

| Task | Read this skill file |
|------|---------------------|
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->
