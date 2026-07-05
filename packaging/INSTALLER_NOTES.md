# MENYOO Watch Dogs Trainer Packaging Notes

These notes describe the intended public installer flow.

## Files

Release payload:

- `Watch-Dogs-MENYOO.exe`
- `WatchDogsTrainer.dll`

Both files must be installed into the same program folder.

## Installer Behavior

- Install the release payload into a normal program folder, for example `%ProgramFiles%\MENYOO Watch Dogs Trainer`.
- Create a Start Menu shortcut named `MENYOO Watch Dogs Trainer`.
- Create an optional Desktop shortcut named `MENYOO Watch Dogs Trainer`.
- Shortcuts should point to `Watch-Dogs-MENYOO.exe`.
- Users should only open the launcher shortcut. They should not need to open Steam separately or use a second shortcut.

## First Launch

- The launcher opens a GUI window titled `MENYOO Watch Dogs Trainer`.
- On first launch, the launcher asks the user to select `watch_dogs.exe`.
- The selected folder is saved as the Watch Dogs game path.
- The launcher should not ask again unless the saved folder no longer exists, `watch_dogs.exe` is missing, or the user clicks `Change Game Path`.

## Config

Config is stored in AppData:

- Launcher config: `%APPDATA%\MENYOO Watch Dogs Trainer\launcher.cfg`
- Trainer config: `%APPDATA%\MENYOO Watch Dogs Trainer\WatchDogsTrainer.json`

The launcher config stores the Watch Dogs install folder. The trainer config stores cheat states, scalar settings, the menu hotkey, and optional user-assigned cheat hotkeys.

## Runtime Flow

1. User opens `MENYOO Watch Dogs Trainer`.
2. Launcher validates the saved Watch Dogs path.
3. `Launch Game` starts `watch_dogs.exe` from the saved path.
4. If `watch_dogs.exe` is already running, the launcher does not start a second copy.
5. The launcher waits for `watch_dogs.exe`, injects `WatchDogsTrainer.dll`, then reports `Trainer loaded`.
6. Logs continue to be written to `loader.log` next to `Watch-Dogs-MENYOO.exe`.
