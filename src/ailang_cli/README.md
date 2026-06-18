# AiLang Native Launcher

Native C CLI entrypoint and host adapter code owned by AiVM.

This code was migrated out of AiLang so AiLang can remain an AiLang-authored
compiler/toolset repository. It is not the desired long-term implementation of
the AiLang CLI. AiVM owns the temporary native bootstrap host, VM execution,
host syscall adapters, and launcher packaging until command behavior is
rewritten in AiLang above the VM boundary.

Current:
- Canonical client-tool execution chain is `ailang -> aivm -> compiled app`.
- `ailang.c` provides the temporary deterministic native C bootstrap
  executable for `tools/ailang`.
- C VM is the default runtime (no flag required).
- `--vm=c` is an explicit alias of the default runtime.
- `--vm=cvN` is reserved for future C VM profile/version selection; currently it maps to `c`.
- `--vm=ast` remains debug-only and is not supported by native runtime execution.
- `build.sh` is the canonical bootstrap entrypoint on Unix-like hosts.
- `build.ps1` is the canonical bootstrap entrypoint on Windows hosts.
- `.aibc1` runtime execution is C-only.
- `build` command is available: `ailang build <program|project-dir> [--out <dir>] [--no-cache]` and emits `app.aibc1`.
- `run` supports deterministic build cache bypass and compiled-app argv passthrough:
  - `ailang run <program|project-dir> [--no-cache] [--] [app-args...]`
  - higher-layer compiled CLIs must preserve indefinite subcommand depth in app argv
- For `debug * run`, place app argv after `--` once any native debug flags (`--out`, `--log-level`, injected input) are present:
  - `ailang debug capture run <app.aibc1> --out <dir> -- debug snapshot`
- Built-in live debug sequencing is available for interactive apps:
  - `--inject-click <x,y>`
  - `--inject-text <text>`
  - `--inject-key <name>`
  - `--inject-wait <polls>`
  - `--inject-close`
  - `--inject-script <path>`
- Built-in host DNS diagnostics are available:
  - `ailang debug dns <host> [port]`
  - success prints `Ok#ok1(type=string value="<ipv4>")`
  - failure prints `Err#err1(code=NET001 ...)` with `detail="dns_failed:..."`
- Target debug/runtime direction is specified in `../../SPEC/DEBUGGING.md`.
  Production `aivm` remains limited; `aivm-debug` owns full debugger,
  profiler, stack trace, capture, replay, and agent-readable artifact support.
- Initial native `aivm-debug` artifact support is available:
  - `aivm-debug debug capture run <app.aibc1> --out <debug-run-dir>`
  - optional profile selection:
    `--profile production|debug|tooling` (default: `debug`)
  - optional capability policy overrides:
    `--allow <group>` and `--deny <group>`
  - `aivm-debug explain <debug-run-dir>`
  - `aivm-debug inspect stack <debug-run-dir>`
  - `aivm-debug inspect memory <debug-run-dir>`
  - `aivm-debug inspect profile <debug-run-dir>`
  - `aivm-debug inspect syscalls <debug-run-dir>`
  - `aivm-debug suggest <debug-run-dir>`
  - `aivm-debug compare <left-debug-run-dir> <right-debug-run-dir>`
  - current artifacts include `config.toml`, `diagnostics.toml`,
    `stdout.txt`, `stderr.txt`, `vm_trace.toml`, `syscall_trace.toml`,
    `stack_trace.toml`, `profile.toml`, `memory.toml`, and
    `suggestions.toml`.
- `--inject-script` uses one command per line with the same `debug interact run` vocabulary:
  - `click 124,138`
  - `text 76103`
  - `key enter`
  - `wait 30`
  - `close`
- `clean` command clears native build cache for a project: `ailang clean [program|project-dir]`.
- Canonical higher-layer CLI option syntax is GNU-style `--full-name` / `-f`; slash-prefixed flags are not part of the AiVectra CLI contract.
- Source/project `run` compiles through native C paths only (no backend delegation).
- `.aibundle` runtime execution is native-only (Bytecode# bundle shape).
- Native `Bytecode#...` `.aos` inputs run directly in C VM without backend fallback.
- Native `publish` can emit `app.aibc1` from supported `Program#...`/`Bytecode#...` `.aos`; unsupported source/project compile shapes return deterministic `DEV008`.
- Native `publish --target wasm32` emits wasm runtime package outputs:
  - profile `spa`/`web` (default): `<app>.wasm`, `app.aibc1`, `aivm-runtime-wasm32-web.mjs`, `index.html`, `main.js`
  - profile `cli`: `<app>.wasm`, `app.aibc1`, `run.sh`, `run.ps1`
  - profile `fullstack`: root wasm payload plus `client/` web package and `server/README.md`
- Native `Program#...`/`Bytecode#...` supported subsets run/publish without backend fallback.
- `serve` is intentionally not part of native runtime surface; native runtime returns deterministic `DEV008` for `serve`.
- `publish` writes a ready-to-run app executable named from project/app input (run as `./<appname>`), plus `app.aibc1`.
- `project.aiproj` can set publish default target via `publishTarget="<rid>"` (or single-entry `publishTargets="..."`).

Target end-state:
- CLI arg parsing and mode selection
- syscall host binding
- direct native core/vm execution only (no backend-host dependency)
