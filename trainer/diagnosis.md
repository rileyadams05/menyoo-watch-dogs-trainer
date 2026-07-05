# Watch Dogs Trainer Diagnostic Notes

## Immediate DLL Execution Path
1. **DllMain (PROCESS_ATTACH)**
   - Records entry into `DllMain`, calls `DisableThreadLibraryCalls`, and spawns a `BootstrapThread` (no heavy work on the loader lock).
2. **Bootstrap thread**
   - Resolves the DLL directory, initialises `trainer_dll.log`, logs pointer size/paths, loads `trainer_debug.json`, and starts `Process::StartWatchThread()`.
3. **Process::StartWatchThread**
   - Copies the diagnostic configuration, logs current mode, launches the watch thread.
4. **Watch thread loop**
   - Logs start, polls for `Disrupt_b64.dll`, sleeps 800 ms once it appears, then calls `Initialize()`.
5. **Process::Initialize**
   - Logs module bases, pattern-scan status, hook/overlay/cheat toggles.
   - In diagnostic mode, skips pattern scanning, MinHook installation, DX11 hook, config load, and cheat activation.
   - If everything succeeds, marks `g_gameReady = true` and logs completion.
6. **Steady state**
   - 16 ms tick only calls `CheatManager::Tick()` when cheats are enabled (disabled in diagnostic build).

## Diagnostic Mode Adjustments
- `trainer_debug.json` defaults to:
  ```json
  {
    "diagnosticMode": true,
    "enableOverlay": false,
    "enableHooks": false,
    "enableCheats": false,
    "enableMemoryPatches": false,
    "enablePatternScan": false,
    "enableHotkeys": false,
    "enablePipeOrIpc": true
  }
  ```
- When `diagnosticMode=true`:
  - Pattern scanning, MinHook installs, DX11 overlay, config-based cheat auto-activation, and hotkey capture are all bypassed.
  - Cheat tick loop remains idle, preventing memory writes or pointer follow-up work.
  - UI adapts to show “Diagnostic” status and disables menu hotkeys when overlay is off.

## Likely Crash Suspects (from full build)
1. **Pattern scanning / pointer resolution** – repeated AOB scans and raw pointer dereferences in `ResolveGamePointers()`.
2. **Hook installation** – MinHook trampolines plus custom JMP patches inside `Hooks::InstallAll()` (covers health, ammo, timers, etc.).
3. **DX11 swap-chain hook** – dummy device creation + Present/Resize vtable swaps in `DX11Hook::Install()`.
4. **Config-driven cheat activation** – `Config::ApplyAll()` toggles cheats immediately after load (each cheat writes directly into game memory).
5. **Hotkeys/UI** – WndProc subclassing and ImGui initialisation could fail if the DX hook runs too early.

## Sequential Re-Enable Plan
Follow the staged approach to pinpoint the crashing system. Toggle each flag in `trainer_debug.json`, one at a time, re-inject, and observe game stability + `trainer_dll.log` output.

1. **Step A – DLL load only**: keep all features disabled (current diagnostic default). Expect game to stay open ≥30 s.
2. **Step B – IPC/communication**: if applicable, enable only `enablePipeOrIpc` (already true). Confirm no crash.
3. **Step C – Hotkeys/input**: toggle `enableHotkeys=true` (overlay still off). Verify keyboard handling alone is safe.
4. **Step D – Pattern scan**: enable `enablePatternScan=true` (leave hooks/overlay off) to validate AOB scanning.
5. **Step E – Overlay only**: turn on `enableOverlay=true` to exercise DX11 hook without gameplay cheats.
6. **Step F – Hooks without patches**: set `enableHooks=true` while keeping `enableMemoryPatches=false` to test detours.
7. **Step G – Safe cheat**: enable `enableCheats=true` (optionally choose a single benign cheat) and finally `enableMemoryPatches=true` once previous stages are stable.

## Logs & Artifacts
- **DLL log**: `release/steam/trainer_dll.log` – detailed timestamped trace of every startup step, configuration, and hook decision.
- **Loader log**: `release/steam/loader.log` – injection chronology, 30 s survival outcome, diagnostic mode flag, and architecture summary (Loader/DLL/Game).
- **Diagnostic config**: `release/steam/trainer_debug.json` – edit between test runs to toggle systems.
- **Architecture check**: loader logs report `Loader=x64, Trainer DLL=x64, Game=x64` (or current state) for sanity.

## Next Actions for Testing
1. Launch Watch Dogs (Steam build), wait on a safe in-game state.
2. Run `Watch-Dogs-MENYOO.exe`; allow auto-inject (15 s stabilization + 30 s post-injection monitor).
3. Confirm status text reads “Diagnostic loaded” and game remains running ≥30 s.
4. Inspect `trainer_dll.log` for the successful initialization sequence.
5. Proceed with staged feature toggles (Steps B–G) to isolate the first failing subsystem, capturing logs after each change.
