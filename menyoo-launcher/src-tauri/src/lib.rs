use chrono::Utc;
use serde::Serialize;
use std::collections::{HashMap, HashSet};
use std::ffi::{c_void, OsStr};
use std::fs::{self, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
#[cfg(windows)]
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime};

#[cfg(windows)]
use widestring::U16CStr;

use tauri::{AppHandle, Emitter, Manager, State};
use tauri_plugin_opener::OpenerExt;

#[cfg(windows)]
use windows::core::{w, Error as WinError, PCSTR};
#[cfg(windows)]
use windows::Win32::Foundation::{CloseHandle, GetLastError, WAIT_TIMEOUT};
#[cfg(windows)]
const SYNCHRONIZE: PROCESS_ACCESS_RIGHTS = PROCESS_ACCESS_RIGHTS(0x00100000);
#[cfg(windows)]
use windows::Win32::System::Diagnostics::Debug::WriteProcessMemory;
#[cfg(windows)]
use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
#[cfg(windows)]
use windows::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};
#[cfg(windows)]
use windows::Win32::System::Memory::{
    VirtualAllocEx, VirtualFreeEx, MEM_COMMIT, MEM_RELEASE, MEM_RESERVE, PAGE_READWRITE,
};
#[cfg(windows)]
use windows::Win32::System::Threading::{
    CreateRemoteThread, GetExitCodeThread, OpenProcess, WaitForSingleObject, PROCESS_ACCESS_RIGHTS,
    PROCESS_CREATE_THREAD, PROCESS_QUERY_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION,
    PROCESS_VM_OPERATION, PROCESS_VM_READ, PROCESS_VM_WRITE,
};
#[cfg(windows)]
use windows::Win32::UI::Input::KeyboardAndMouse::{
    GetKeyNameTextW, MapVirtualKeyW, MAPVK_VK_TO_VSC, VK_DELETE, VK_DOWN, VK_END, VK_HOME,
    VK_INSERT, VK_LEFT, VK_NEXT, VK_PRIOR, VK_RIGHT, VK_UP,
};

const TARGET_EXE: &str = "watch_dogs.exe";
const STABILIZATION_SECONDS: u64 = 15;
const INJECTION_TIMEOUT_SECONDS: u64 = 10;
const VERIFY_SECONDS: u64 = 30;
const GRACE_SECONDS: u64 = 5;

#[derive(Clone)]
struct AppState {
    inner: Arc<Mutex<InternalState>>,
    paths: Paths,
}

impl AppState {
    fn new(paths: Paths) -> Self {
        let status = TrainerStatus::default();
        Self {
            inner: Arc::new(Mutex::new(InternalState::new(status))),
            paths,
        }
    }
}

struct InternalState {
    status: TrainerStatus,
    wait_started_at: Option<Instant>,
    verify_started_at: Option<Instant>,
    injection_started_at: Option<Instant>,
    last_serialized: Option<String>,
    attempted_pids: HashSet<u32>,
    injection_rx: Option<Receiver<InjectionOutcome>>,
    pending_pid: Option<u32>,
    grace_until: Option<(u32, Instant)>,
    diag_mtime: Option<SystemTime>,
    trainer_config_mtime: Option<SystemTime>,
    last_error: Option<String>,
}

impl InternalState {
    fn new(status: TrainerStatus) -> Self {
        Self {
            status,
            wait_started_at: None,
            verify_started_at: None,
            injection_started_at: None,
            last_serialized: None,
            attempted_pids: HashSet::new(),
            injection_rx: None,
            pending_pid: None,
            grace_until: None,
            diag_mtime: None,
            trainer_config_mtime: None,
            last_error: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub enum GamePlatform {
    Steam,
    UbisoftConnect,
    EpicGames,
    Manual,
    Unknown,
}

#[derive(Debug, Clone, Serialize)]
struct TrainerStatus {
    process_name: String,
    game_on: bool,
    process_id: u32,
    trainer_stage: TrainerStage,
    status_message: Option<String>,
    verifying_seconds_remaining: Option<u64>,
    diagnostic_mode: bool,
    cheats_disabled_reason: Option<String>,
    last_error: Option<String>,
    cheats: Vec<CheatEntry>,
    timestamp: String,
    pub game_platform: GamePlatform,
    pub game_path: Option<String>,
}

impl Default for TrainerStatus {
    fn default() -> Self {
        Self {
            process_name: TARGET_EXE.to_string(),
            game_on: false,
            process_id: 0,
            trainer_stage: TrainerStage::NotLoaded,
            status_message: Some("Waiting for Watch Dogs...".into()),
            verifying_seconds_remaining: None,
            diagnostic_mode: false,
            cheats_disabled_reason: None,
            last_error: None,
            cheats: build_default_cheats(),
            timestamp: Utc::now().to_rfc3339(),
            game_platform: GamePlatform::Unknown,
            game_path: None,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
struct CheatEntry {
    id: String,
    name: String,
    category: String,
    safe: bool,
    enabled: bool,
    locked: bool,
    hotkey_label: Option<String>,
    hotkey_vk: Option<u32>,
}

#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
enum TrainerStage {
    NotLoaded,
    Waiting,
    Injecting,
    Verifying,
    DiagnosticLoaded,
    Failed,
}

impl TrainerStage {
    fn label(self) -> &'static str {
        match self {
            TrainerStage::NotLoaded => "Not loaded",
            TrainerStage::Waiting => "Waiting",
            TrainerStage::Injecting => "Injecting",
            TrainerStage::Verifying => "Verifying",
            TrainerStage::DiagnosticLoaded => "Diagnostic Loaded",
            TrainerStage::Failed => "Failed",
        }
    }
}

#[derive(Clone)]
struct Paths {
    release_dir: PathBuf,
    dll_path: PathBuf,
    loader_log: PathBuf,
    trainer_log: PathBuf,
    diagnostic_config: PathBuf,
    trainer_config: PathBuf,
}

impl Paths {
    fn discover() -> Result<Self, String> {
        let exe = std::env::current_exe().map_err(|e| e.to_string())?;
        let release_dir = exe
            .parent()
            .ok_or_else(|| "Failed to resolve executable directory".to_string())?
            .to_path_buf();
        let dll_path = release_dir.join("WatchDogsTrainer.dll");
        let loader_log = release_dir.join("loader.log");
        let trainer_log = release_dir.join("trainer_dll.log");
        let diagnostic_config = release_dir.join("trainer_debug.json");

        let config_base = dirs::config_dir()
            .unwrap_or_else(|| release_dir.clone())
            .join("MENYOO Watch Dogs Trainer");
        if let Err(err) = fs::create_dir_all(&config_base) {
            return Err(format!(
                "Failed to create config directory {:?}: {}",
                config_base, err
            ));
        }
        let trainer_config = config_base.join("WatchDogsTrainer.json");

        Ok(Self {
            release_dir,
            dll_path,
            loader_log,
            trainer_log,
            diagnostic_config,
            trainer_config,
        })
    }
}

#[derive(Debug)]
struct InjectionOutcome {
    success: bool,
    error_code: u32,
    message: Option<String>,
    pid: u32,
}

#[derive(Debug)]
#[cfg(windows)]
struct InjectionError {
    stage: &'static str,
    code: u32,
    message: String,
}

#[cfg(windows)]
impl InjectionError {
    fn from_win_error(stage: &'static str, err: WinError) -> Self {
        let code = err.code().0 as u32;
        let message = {
            let msg = err.message().to_string();
            if msg.trim().is_empty() {
                format_windows_error(code)
            } else {
                msg
            }
        };
        Self {
            stage,
            code,
            message,
        }
    }
}

struct CheatDefinition {
    id: &'static str,
    name: &'static str,
    category: &'static str,
    safe: bool,
}

const CHEAT_DEFINITIONS: &[CheatDefinition] = &[
    CheatDefinition {
        id: "godmode",
        name: "God Mode",
        category: "Player",
        safe: true,
    },
    CheatDefinition {
        id: "godmode",
        name: "Infinite Health",
        category: "Player",
        safe: true,
    },
    CheatDefinition {
        id: "inffocus",
        name: "Infinite Focus",
        category: "Player",
        safe: true,
    },
    CheatDefinition {
        id: "infbattery",
        name: "Infinite Battery",
        category: "Player",
        safe: true,
    },
    CheatDefinition {
        id: "infskillpts",
        name: "Infinite Skill Points",
        category: "Player",
        safe: false,
    },
    CheatDefinition {
        id: "infmoney",
        name: "Infinite Money",
        category: "Player",
        safe: false,
    },
    CheatDefinition {
        id: "infxp",
        name: "Infinite XP",
        category: "Player",
        safe: false,
    },
    CheatDefinition {
        id: "lockrep",
        name: "Reputation",
        category: "Player",
        safe: false,
    },
    CheatDefinition {
        id: "notoriety",
        name: "Notoriety",
        category: "Player",
        safe: false,
    },
    CheatDefinition {
        id: "lockammo",
        name: "Infinite Ammo",
        category: "Weapons / Items",
        safe: true,
    },
    CheatDefinition {
        id: "lockammo",
        name: "No Reload",
        category: "Weapons / Items",
        safe: true,
    },
    CheatDefinition {
        id: "lockcraft",
        name: "Infinite Craft Materials",
        category: "Weapons / Items",
        safe: true,
    },
    CheatDefinition {
        id: "refillwheel",
        name: "Infinite Items",
        category: "Weapons / Items",
        safe: false,
    },
    CheatDefinition {
        id: "stealth",
        name: "Invisible",
        category: "Stealth / Police",
        safe: false,
    },
    CheatDefinition {
        id: "stealth",
        name: "Undetectable",
        category: "Stealth / Police",
        safe: false,
    },
    CheatDefinition {
        id: "clearheat",
        name: "Wanted Level Control",
        category: "Stealth / Police",
        safe: true,
    },
    CheatDefinition {
        id: "clearheat",
        name: "Police Radar / Heat Control",
        category: "Stealth / Police",
        safe: true,
    },
    CheatDefinition {
        id: "noclip",
        name: "No Clip / Free Roam",
        category: "World / Movement",
        safe: false,
    },
    CheatDefinition {
        id: "oneteleport",
        name: "Teleport To Waypoint",
        category: "World / Movement",
        safe: false,
    },
    CheatDefinition {
        id: "savecords",
        name: "Save Coordinates",
        category: "World / Movement",
        safe: false,
    },
    CheatDefinition {
        id: "restorecords",
        name: "Restore Coordinates",
        category: "World / Movement",
        safe: false,
    },
    CheatDefinition {
        id: "settime",
        name: "Time Of Day",
        category: "World / Movement",
        safe: false,
    },
    CheatDefinition {
        id: "overidefov",
        name: "FOV / Camera Distance",
        category: "World / Movement",
        safe: false,
    },
    CheatDefinition {
        id: "onehitcar",
        name: "Car Health",
        category: "Vehicles",
        safe: false,
    },
    CheatDefinition {
        id: "onehitcar",
        name: "One Hit Destroy Vehicles",
        category: "Vehicles",
        safe: false,
    },
    CheatDefinition {
        id: "savecords",
        name: "Vehicle Coordinates",
        category: "Vehicles",
        safe: false,
    },
    CheatDefinition {
        id: "infhacktime",
        name: "Infinite Hacking Time",
        category: "Mini Games / Digital Trips",
        safe: true,
    },
    CheatDefinition {
        id: "spidertimer",
        name: "Spider Tank Cheats",
        category: "Mini Games / Digital Trips",
        safe: true,
    },
    CheatDefinition {
        id: "cashrunlock",
        name: "Cash Run",
        category: "Mini Games / Digital Trips",
        safe: true,
    },
    CheatDefinition {
        id: "nvznlock",
        name: "NVZN",
        category: "Mini Games / Digital Trips",
        safe: true,
    },
    CheatDefinition {
        id: "madnesstimer",
        name: "Madness Timer",
        category: "Mini Games / Digital Trips",
        safe: true,
    },
    CheatDefinition {
        id: "ctosstop",
        name: "ctOS Timer",
        category: "Mini Games / Digital Trips",
        safe: true,
    },
    CheatDefinition {
        id: "infskillpts",
        name: "Unlock All",
        category: "Misc",
        safe: false,
    },
    CheatDefinition {
        id: "onehitcar",
        name: "One Hit Kill",
        category: "Misc",
        safe: false,
    },
    CheatDefinition {
        id: "poker1",
        name: "Poker Money",
        category: "Misc",
        safe: false,
    },
];

fn build_default_cheats() -> Vec<CheatEntry> {
    CHEAT_DEFINITIONS
        .iter()
        .map(|def| CheatEntry {
            id: def.id.to_string(),
            name: def.name.to_string(),
            category: def.category.to_string(),
            safe: def.safe,
            enabled: false,
            locked: true,
            hotkey_label: None,
            hotkey_vk: None,
        })
        .collect()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let paths = match Paths::discover() {
        Ok(p) => p,
        Err(err) => {
            eprintln!("Failed to initialise paths: {}", err);
            panic!("Failed to initialise trainer paths");
        }
    };
    let app_state = AppState::new(paths.clone());

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .manage(app_state.clone())
        .setup(move |app| {
            ensure_log_files(&paths);
            append_loader_log(&paths, "MENYOO Watch Dogs Trainer launcher started.");

            if let Some(window) = app.get_webview_window("main") {
                let icon = tauri::image::Image::from_bytes(include_bytes!("../icons/Icon.ico"))?;
                window.set_icon(icon)?;
            }

            let handle = app.handle().clone();
            let state = app.state::<AppState>().inner().clone();
            tauri::async_runtime::spawn(async move {
                monitoring_loop(handle, state).await;
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_status,
            read_loader_log,
            read_trainer_log,
            open_release_folder,
            exit_app,
            activate_all,
            deactivate_all,
            set_cheat_state,
            set_cheat_hotkey,
            clear_cheat_hotkey,
            set_manual_game_path
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

async fn monitoring_loop(app: AppHandle, state: AppState) {
    loop {
        if let Err(err) = monitor_tick(&app, &state) {
            append_loader_log(&state.paths, &format!("Monitor error: {}", err));
        }
        std::thread::sleep(Duration::from_secs(1));
    }
}

fn monitor_tick(app: &AppHandle, shared: &AppState) -> Result<(), String> {
    let (diagnostic_mode, diag_mtime) = read_diagnostic_mode(&shared.paths);
    let mut trainer_config = TrainerConfig::load(&shared.paths.trainer_config);
    let config_mtime = trainer_config.modified;

    if diagnostic_mode {
        enforce_diagnostic_restrictions(&shared.paths, &mut trainer_config)?;
    }

    let cheats = build_cheat_entries(&trainer_config, diagnostic_mode);
    let pid = find_game_pid();
    let now = Instant::now();

    let mut guard = shared
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;

    let (platform, game_path) = detect_game_install(trainer_config.manual_game_path());

    if guard.status.diagnostic_mode != diagnostic_mode {
        guard.status.diagnostic_mode = diagnostic_mode;
        guard.status.cheats_disabled_reason = if diagnostic_mode {
            Some("Cheats disabled in diagnostic mode.".into())
        } else {
            None
        };
    }
    guard.status.cheats = cheats;
    guard.diag_mtime = diag_mtime;
    guard.trainer_config_mtime = config_mtime;
    guard.status.game_platform = platform;
    guard.status.game_path = game_path;

    guard.status.timestamp = Utc::now().to_rfc3339();

    match pid {
        Some(process_id) => {
            guard.status.game_on = true;
            guard.status.process_id = process_id;

            if guard.pending_pid != Some(process_id) {
                guard.pending_pid = Some(process_id);
                guard.wait_started_at = Some(now);
                guard.verify_started_at = None;
                guard.injection_started_at = None;
                guard.injection_rx = None;
                guard.status.trainer_stage = TrainerStage::Waiting;
                guard.status.status_message =
                    Some("Waiting for Watch Dogs to stabilise (15s) before injection.".into());
                guard.grace_until = None;
                guard.last_error = None;
            }

            if let Some(rx) = guard.injection_rx.as_ref() {
                if let Ok(result) = rx.try_recv() {
                    guard.injection_rx = None;
                    guard.injection_started_at = None;
                    guard.attempted_pids.insert(process_id);
                    if result.success {
                        guard.status.trainer_stage = TrainerStage::Verifying;
                        guard.verify_started_at = Some(now);
                        guard.status.status_message =
                            Some("Verifying diagnostic DLL load (30s).".into());
                        guard.status.last_error = None;
                        append_loader_log(
                            &shared.paths,
                            "Injection completed. Beginning 30-second diagnostic verification.",
                        );
                    } else {
                        guard.status.trainer_stage = TrainerStage::Failed;
                        let msg = result.message.unwrap_or_else(|| {
                            "Injection failed. See loader.log for details.".into()
                        });
                        guard.status.status_message = Some(msg.clone());
                        guard.status.last_error = Some(msg.clone());
                        guard.last_error = Some(msg.clone());
                        append_loader_log(
                            &shared.paths,
                            &format!("Injection failed for PID {}: {}", process_id, msg),
                        );
                    }
                }
            }

            match guard.status.trainer_stage {
                TrainerStage::Waiting => {
                    if let Some(start) = guard.wait_started_at {
                        let elapsed = now.saturating_duration_since(start);
                        let remaining = STABILIZATION_SECONDS.saturating_sub(elapsed.as_secs());
                        if remaining > 0 {
                            guard.status.status_message =
                                Some(format!("Injecting in {}s...", remaining));
                        }
                        if elapsed.as_secs() >= STABILIZATION_SECONDS
                            && !guard.attempted_pids.contains(&process_id)
                            && guard.injection_rx.is_none()
                        {
                            start_injection(&shared.paths, &mut guard, process_id)?;
                        }
                    } else {
                        guard.wait_started_at = Some(now);
                    }
                }
                TrainerStage::Injecting => {
                    if let Some(start) = guard.injection_started_at {
                        if now.saturating_duration_since(start).as_secs()
                            > INJECTION_TIMEOUT_SECONDS
                        {
                            guard.status.trainer_stage = TrainerStage::Failed;
                            let msg = "Injection timed out. Run the trainer as administrator and ensure Watch Dogs is fully loaded.".to_string();
                            guard.status.status_message = Some(msg.clone());
                            guard.status.last_error = Some(msg.clone());
                            guard.last_error = Some(msg.clone());
                            guard.injection_rx = None;
                            guard.injection_started_at = None;
                            guard.attempted_pids.insert(process_id);
                            append_loader_log(
                                &shared.paths,
                                &format!("Injection timed out for PID {}", process_id),
                            );
                        } else {
                            guard.status.status_message = Some("Injecting trainer...".into());
                        }
                    }
                }
                TrainerStage::Verifying => {
                    if let Some(start) = guard.verify_started_at {
                        let elapsed = now.saturating_duration_since(start);
                        if !is_process_alive(process_id) {
                            guard.status.trainer_stage = TrainerStage::Failed;
                            let msg = "Watch Dogs closed during diagnostic verification. See loader.log for details.".to_string();
                            guard.status.status_message = Some(msg.clone());
                            guard.status.last_error = Some(msg.clone());
                            guard.last_error = Some(msg.clone());
                            guard.verify_started_at = None;
                            guard.attempted_pids.insert(process_id);
                            append_loader_log(
                                &shared.paths,
                                "Diagnostic verification failed: process terminated early.",
                            );
                        } else if elapsed.as_secs() >= VERIFY_SECONDS {
                            guard.status.trainer_stage = TrainerStage::DiagnosticLoaded;
                            guard.status.status_message =
                                Some("Diagnostic DLL loaded successfully.".into());
                            guard.status.verifying_seconds_remaining = None;
                            guard.verify_started_at = None;
                            guard.last_error = None;
                            append_loader_log(
                                &shared.paths,
                                "Diagnostic DLL remained loaded after 30 seconds.",
                            );
                        } else {
                            guard.status.verifying_seconds_remaining =
                                Some(VERIFY_SECONDS - elapsed.as_secs());
                            guard.status.status_message = Some(format!(
                                "Verifying diagnostic DLL ({}s remaining)...",
                                VERIFY_SECONDS - elapsed.as_secs()
                            ));
                        }
                    }
                }
                TrainerStage::DiagnosticLoaded => {
                    guard.status.verifying_seconds_remaining = None;
                    guard.status.status_message = Some("Diagnostic DLL loaded.".into());
                }
                TrainerStage::Failed => {
                    if let Some(err) = guard.last_error.clone() {
                        guard.status.status_message = Some(err);
                    }
                }
                TrainerStage::NotLoaded => {}
            }
        }
        None => {
            // Process not found
            if let Some((pid_lost, grace_start)) = guard.grace_until {
                if now.saturating_duration_since(grace_start).as_secs() >= GRACE_SECONDS {
                    guard.status.game_on = false;
                    guard.status.process_id = 0;
                    guard.status.trainer_stage = TrainerStage::NotLoaded;
                    guard.status.status_message = Some("Waiting for Watch Dogs...".into());
                    guard.pending_pid = None;
                    guard.wait_started_at = None;
                    guard.verify_started_at = None;
                    guard.injection_started_at = None;
                    guard.injection_rx = None;
                    guard.status.verifying_seconds_remaining = None;
                    guard.grace_until = None;
                    guard.last_error = None;
                    append_loader_log(
                        &shared.paths,
                        &format!(
                            "watch_dogs.exe (PID {}) closed. Returning to idle state.",
                            pid_lost
                        ),
                    );
                }
            } else if guard.status.game_on {
                guard.grace_until = Some((guard.status.process_id, now));
                append_loader_log(
                    &shared.paths,
                    &format!(
                        "Tracked PID {} disappeared. Starting {}s grace period.",
                        guard.status.process_id, GRACE_SECONDS
                    ),
                );
            } else {
                guard.status.game_on = false;
                guard.status.process_id = 0;
                guard.status.trainer_stage = TrainerStage::NotLoaded;
                guard.status.status_message = Some("Waiting for Watch Dogs...".into());
                guard.status.verifying_seconds_remaining = None;
                guard.pending_pid = None;
                guard.wait_started_at = None;
            }
        }
    }

    guard.status.timestamp = Utc::now().to_rfc3339();
    guard.status.cheats_disabled_reason = if guard.status.diagnostic_mode {
        Some("Cheats disabled in diagnostic mode.".into())
    } else {
        None
    };
    guard.status.last_error = guard.last_error.clone();

    let payload = guard.status.clone();
    let serialized = serde_json::to_string(&payload).map_err(|e| e.to_string())?;
    let should_emit = guard
        .last_serialized
        .as_ref()
        .map(|prev| prev != &serialized)
        .unwrap_or(true);
    guard.last_serialized = Some(serialized);
    drop(guard);

    if should_emit {
        let _ = app.emit("status-update", payload);
    }

    Ok(())
}

#[cfg(windows)]
fn start_injection(paths: &Paths, guard: &mut InternalState, pid: u32) -> Result<(), String> {
    append_loader_log(paths, &format!("Starting injection for PID {}", pid));
    let dll_path = paths.dll_path.clone();
    let (tx, rx) = mpsc::channel();
    thread::spawn(move || {
        let outcome = perform_injection(pid, &dll_path);
        let _ = tx.send(outcome);
    });
    guard.injection_rx = Some(rx);
    guard.injection_started_at = Some(Instant::now());
    guard.status.trainer_stage = TrainerStage::Injecting;
    guard.status.status_message = Some("Injecting trainer...".into());
    guard.last_error = None;
    Ok(())
}

#[cfg(not(windows))]
fn start_injection(paths: &Paths, guard: &mut InternalState, _pid: u32) -> Result<(), String> {
    guard.status.trainer_stage = TrainerStage::Failed;
    let msg =
        "Trainer backend is currently Windows-only. Linux/Proton support is not implemented yet."
            .to_string();
    guard.status.status_message = Some(msg.clone());
    guard.status.last_error = Some(msg.clone());
    guard.last_error = Some(msg.clone());
    append_loader_log(paths, &msg);
    Ok(())
}

#[cfg(windows)]
fn perform_injection(pid: u32, dll_path: &Path) -> InjectionOutcome {
    match inject_trainer(pid, dll_path) {
        Ok(_) => InjectionOutcome {
            success: true,
            error_code: 0,
            message: None,
            pid,
        },
        Err(err) => InjectionOutcome {
            success: false,
            error_code: err.code,
            message: Some(format!("{}: {}", err.stage, err.message)),
            pid,
        },
    }
}

#[cfg(windows)]
fn win_error(stage: &'static str, err: WinError) -> InjectionError {
    InjectionError::from_win_error(stage, err)
}

#[cfg(windows)]
fn inject_trainer(pid: u32, dll_path: &Path) -> Result<(), InjectionError> {
    unsafe {
        let process = OpenProcess(
            PROCESS_CREATE_THREAD
                | PROCESS_QUERY_INFORMATION
                | PROCESS_VM_OPERATION
                | PROCESS_VM_WRITE
                | PROCESS_VM_READ,
            false,
            pid,
        )
        .map_err(|e| win_error("OpenProcess", e))?;

        let dll_wide = to_wide_null(dll_path.as_os_str());
        let byte_len = dll_wide.len() * std::mem::size_of::<u16>();
        let remote = VirtualAllocEx(
            process,
            None,
            byte_len,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE,
        );
        if remote.is_null() {
            let err = GetLastError().0;
            let _ = CloseHandle(process);
            return Err(InjectionError {
                stage: "VirtualAllocEx",
                code: err,
                message: format_windows_error(err),
            });
        }

        match WriteProcessMemory(
            process,
            remote,
            dll_wide.as_ptr() as *const c_void,
            byte_len,
            None,
        ) {
            Ok(_) => {}
            Err(e) => {
                let _ = VirtualFreeEx(process, remote, 0, MEM_RELEASE);
                let _ = CloseHandle(process);
                return Err(win_error("WriteProcessMemory", e));
            }
        }

        let kernel32 = GetModuleHandleW(w!("kernel32.dll")).map_err(|e| {
            let _ = VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            let _ = CloseHandle(process);
            win_error("GetModuleHandleW", e)
        })?;

        let proc_name = b"LoadLibraryW\0";
        let load_lib = GetProcAddress(kernel32, PCSTR(proc_name.as_ptr())).ok_or_else(|| {
            let _ = VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            let _ = CloseHandle(process);
            InjectionError {
                stage: "GetProcAddress",
                code: 0,
                message: "GetProcAddress returned null".into(),
            }
        })?;

        let thread_handle = CreateRemoteThread(
            process,
            None,
            0,
            Some(std::mem::transmute(load_lib)),
            Some(remote as *const c_void),
            0,
            None,
        )
        .map_err(|e| {
            let _ = VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            let _ = CloseHandle(process);
            win_error("CreateRemoteThread", e)
        })?;

        WaitForSingleObject(thread_handle, 8000);
        let mut exit_code = 0u32;
        let _ = GetExitCodeThread(thread_handle, &mut exit_code);
        let _ = CloseHandle(thread_handle);
        let _ = VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        let _ = CloseHandle(process);

        if exit_code == 0 {
            return Err(InjectionError {
                stage: "LoadLibraryW",
                code: 0,
                message: "LoadLibraryW returned 0 in target process.".into(),
            });
        }
    }
    Ok(())
}

fn ensure_log_files(paths: &Paths) {
    for log in [&paths.loader_log, &paths.trainer_log] {
        if OpenOptions::new()
            .append(true)
            .create(true)
            .open(log)
            .is_ok()
        {
            continue;
        }
    }
}

fn append_loader_log(paths: &Paths, message: &str) {
    if let Ok(mut file) = OpenOptions::new()
        .append(true)
        .create(true)
        .open(&paths.loader_log)
    {
        let line = format!(
            "[{}] {}\r\n",
            Utc::now().format("%Y-%m-%d %H:%M:%S"),
            message
        );
        let _ = file.write_all(line.as_bytes());
    }
}

fn read_diagnostic_mode(paths: &Paths) -> (bool, Option<SystemTime>) {
    if let Ok(metadata) = fs::metadata(&paths.diagnostic_config) {
        let modified = metadata.modified().ok();
        if let Ok(contents) = fs::read_to_string(&paths.diagnostic_config) {
            if let Ok(json) = serde_json::from_str::<serde_json::Value>(&contents) {
                if let Some(mode) = json.get("diagnosticMode").and_then(|v| v.as_bool()) {
                    return (mode, modified);
                }
            }
        }
        return (false, modified);
    }
    (false, None)
}

fn enforce_diagnostic_restrictions(
    paths: &Paths,
    config: &mut TrainerConfig,
) -> Result<(), String> {
    let mut changed = false;
    for def in CHEAT_DEFINITIONS.iter() {
        if config.is_cheat_enabled(def.id) {
            config.set_cheat(def.id, false);
            changed = true;
        }
    }
    if changed {
        append_loader_log(
            paths,
            "Diagnostics: forced all cheats OFF because diagnosticMode=true.",
        );
        config.save(&paths.trainer_config)?;
    }
    Ok(())
}

fn build_cheat_entries(config: &TrainerConfig, diagnostic_mode: bool) -> Vec<CheatEntry> {
    let hotkeys = config.launcher_hotkeys_map();
    CHEAT_DEFINITIONS
        .iter()
        .map(|def| {
            let mut enabled = config.is_cheat_enabled(def.id);
            if diagnostic_mode {
                enabled = false;
            }
            let hotkey_vk = hotkeys.get(def.name).copied();
            let hotkey_label = hotkey_vk.and_then(vk_to_label);
            CheatEntry {
                id: def.id.to_string(),
                name: def.name.to_string(),
                category: def.category.to_string(),
                safe: def.safe,
                enabled,
                locked: diagnostic_mode,
                hotkey_label,
                hotkey_vk,
            }
        })
        .collect()
}

fn has_game_exe(path: &Path) -> bool {
    path.join("Watch_Dogs.exe").exists()
        || path.join("watch_dogs.exe").exists()
        || path.join("bin").join("Watch_Dogs.exe").exists()
        || path.join("bin").join("watch_dogs.exe").exists()
}

fn detect_game_install(manual_path: Option<String>) -> (GamePlatform, Option<String>) {
    if let Some(path_str) = manual_path {
        let path = Path::new(&path_str);
        if has_game_exe(path) {
            return (GamePlatform::Manual, Some(path_str));
        }
    }

    #[cfg(windows)]
    {
        let common_steam = [
            r"C:\Program Files (x86)\Steam\steamapps\common\Watch_Dogs",
            r"C:\Program Files\Steam\steamapps\common\Watch_Dogs",
            r"D:\SteamLibrary\steamapps\common\Watch_Dogs",
        ];
        for p in common_steam {
            let path = Path::new(p);
            if has_game_exe(path) {
                return (GamePlatform::Steam, Some(p.to_string()));
            }
        }

        let common_ubisoft = [
            r"C:\Program Files (x86)\Ubisoft\Ubisoft Game Launcher\games\Watch_Dogs",
            r"C:\Program Files\Ubisoft\Ubisoft Game Launcher\games\Watch_Dogs",
        ];
        for p in common_ubisoft {
            let path = Path::new(p);
            if has_game_exe(path) {
                return (GamePlatform::UbisoftConnect, Some(p.to_string()));
            }
        }

        let common_epic = [
            r"C:\Program Files\Epic Games\WatchDogs",
            r"C:\Program Files (x86)\Epic Games\WatchDogs",
        ];
        for p in common_epic {
            let path = Path::new(p);
            if has_game_exe(path) {
                return (GamePlatform::EpicGames, Some(p.to_string()));
            }
        }
    }

    #[cfg(not(windows))]
    {
        if let Some(home) = dirs::home_dir() {
            let steam_paths = [
                home.join(".local/share/Steam/steamapps/common/Watch_Dogs"),
                home.join(".steam/root/steamapps/common/Watch_Dogs"),
                home.join(".steam/steam/steamapps/common/Watch_Dogs"),
            ];
            for path in steam_paths {
                if has_game_exe(&path) {
                    return (
                        GamePlatform::Steam,
                        Some(path.to_string_lossy().to_string()),
                    );
                }
            }
        }
    }

    (GamePlatform::Unknown, None)
}

#[cfg(windows)]
fn find_game_pid() -> Option<u32> {
    unsafe {
        let snapshot = match CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) {
            Ok(handle) => handle,
            Err(_) => return None,
        };

        let mut entry = PROCESSENTRY32W {
            dwSize: std::mem::size_of::<PROCESSENTRY32W>() as u32,
            ..Default::default()
        };
        let mut result = None;
        if Process32FirstW(snapshot, &mut entry).is_ok() {
            loop {
                let name = U16CStr::from_ptr_str(entry.szExeFile.as_ptr());
                if name.to_string_lossy().eq_ignore_ascii_case(TARGET_EXE) {
                    result = Some(entry.th32ProcessID);
                    break;
                }
                if Process32NextW(snapshot, &mut entry).is_err() {
                    break;
                }
            }
        }
        let _ = CloseHandle(snapshot);
        result
    }
}

#[cfg(windows)]
fn is_process_alive(pid: u32) -> bool {
    unsafe {
        let process = match OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, false, pid)
        {
            Ok(handle) => handle,
            Err(_) => return false,
        };
        let wait = WaitForSingleObject(process, 0);
        let _ = CloseHandle(process);
        wait == WAIT_TIMEOUT
    }
}

#[cfg(windows)]
fn format_windows_error(code: u32) -> String {
    format!("Windows error {}", code)
}

#[cfg(windows)]
fn to_wide_null(value: &OsStr) -> Vec<u16> {
    let mut wide: Vec<u16> = value.encode_wide().collect();
    wide.push(0);
    wide
}

#[cfg(windows)]
fn vk_to_label(vk: u32) -> Option<String> {
    if vk == 0 {
        return None;
    }
    unsafe {
        let scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        let mut l_param = (scan << 16) as i32;
        let extended_keys = [
            VK_INSERT.0,
            VK_DELETE.0,
            VK_HOME.0,
            VK_END.0,
            VK_PRIOR.0,
            VK_NEXT.0,
            VK_LEFT.0,
            VK_RIGHT.0,
            VK_UP.0,
            VK_DOWN.0,
        ];
        if extended_keys.contains(&(vk as u16)) {
            l_param |= 0x0100_0000;
        }
        let mut buffer = [0u16; 64];
        let len = GetKeyNameTextW(l_param, &mut buffer) as usize;
        if len > 0 {
            Some(String::from_utf16_lossy(&buffer[..len]))
        } else {
            Some(format!("VK_{:02X}", vk))
        }
    }
}

#[cfg(not(windows))]
fn find_game_pid() -> Option<u32> {
    None
}

#[cfg(not(windows))]
fn is_process_alive(_pid: u32) -> bool {
    false
}

#[cfg(not(windows))]
fn vk_to_label(_vk: u32) -> Option<String> {
    None
}

struct TrainerConfig {
    raw: serde_json::Value,
    modified: Option<SystemTime>,
}

impl TrainerConfig {
    fn load(path: &Path) -> Self {
        let (raw, modified) = match fs::read_to_string(path) {
            Ok(content) => {
                let value = serde_json::from_str::<serde_json::Value>(&content)
                    .unwrap_or_else(|_| serde_json::Value::Object(Default::default()));
                let modified = fs::metadata(path).ok().and_then(|m| m.modified().ok());
                (value, modified)
            }
            Err(_) => (serde_json::Value::Object(Default::default()), None),
        };
        Self { raw, modified }
    }

    fn ensure_root(&mut self) -> &mut serde_json::Map<String, serde_json::Value> {
        if !self.raw.is_object() {
            self.raw = serde_json::Value::Object(Default::default());
        }
        self.raw.as_object_mut().unwrap()
    }

    fn ensure_object<'a>(
        map: &'a mut serde_json::Map<String, serde_json::Value>,
        key: &str,
    ) -> &'a mut serde_json::Map<String, serde_json::Value> {
        map.entry(key.to_string())
            .or_insert_with(|| serde_json::Value::Object(Default::default()));
        map.get_mut(key).unwrap().as_object_mut().unwrap()
    }

    fn is_cheat_enabled(&self, id: &str) -> bool {
        self.raw
            .get("cheats")
            .and_then(|v| v.as_object())
            .and_then(|map| map.get(id))
            .and_then(|v| v.as_bool())
            .unwrap_or(false)
    }

    fn set_cheat(&mut self, id: &str, enabled: bool) {
        let root = self.ensure_root();
        let cheats = Self::ensure_object(root, "cheats");
        cheats.insert(id.to_string(), serde_json::Value::Bool(enabled));
    }

    fn set_all(&mut self, enabled: bool) {
        for def in CHEAT_DEFINITIONS.iter() {
            self.set_cheat(def.id, enabled);
        }
    }

    fn manual_game_path(&self) -> Option<String> {
        self.raw
            .get("manualGamePath")
            .and_then(|v| v.as_str())
            .map(|s| s.to_string())
    }

    fn set_manual_game_path(&mut self, path: Option<String>) {
        let root = self.ensure_root();
        match path {
            Some(p) => root.insert("manualGamePath".into(), serde_json::Value::String(p)),
            None => root.remove("manualGamePath"),
        };
    }

    fn launcher_hotkeys_map(&self) -> HashMap<String, u32> {
        let mut map = HashMap::new();
        if let Some(obj) = self.raw.get("launcherHotkeys").and_then(|v| v.as_object()) {
            for (key, value) in obj {
                if let Some(num) = value.as_u64() {
                    map.insert(key.clone(), num as u32);
                }
            }
        }
        map
    }

    fn set_hotkey(&mut self, name: &str, vk: Option<u32>) {
        let root = self.ensure_root();
        let hotkeys = Self::ensure_object(root, "launcherHotkeys");
        match vk {
            Some(code) => {
                hotkeys.insert(name.to_string(), serde_json::Value::Number(code.into()));
            }
            None => {
                hotkeys.remove(name);
            }
        }
    }

    fn rebuild_hotkeys(&mut self) {
        let root = self.ensure_root();
        let launcher_map = root
            .get("launcherHotkeys")
            .and_then(|v| v.as_object())
            .cloned()
            .unwrap_or_default();
        let mut by_id = serde_json::Map::new();
        let mut seen = HashSet::new();
        for def in CHEAT_DEFINITIONS.iter() {
            if !seen.insert(def.id) {
                continue;
            }
            if let Some(value) = launcher_map.get(def.name).and_then(|v| v.as_u64()) {
                by_id.insert(def.id.to_string(), serde_json::Value::Number(value.into()));
            }
        }
        root.insert("hotkeys".into(), serde_json::Value::Object(by_id));
    }

    fn ensure_defaults(&mut self) {
        let root = self.ensure_root();
        root.entry("menuHotkey")
            .or_insert(serde_json::Value::Number((0x5B as u32).into()));
        root.entry("fov").or_insert(serde_json::Value::Number(
            serde_json::Number::from_f64(75.0).unwrap(),
        ));
        root.entry("timeHours").or_insert(serde_json::Value::Number(
            serde_json::Number::from_f64(12.0).unwrap(),
        ));
        root.entry("maxAmmo")
            .or_insert(serde_json::Value::Number(serde_json::Number::from(9999)));
        root.entry("maxCraft")
            .or_insert(serde_json::Value::Number(serde_json::Number::from(999)));
    }

    fn save(&mut self, path: &Path) -> Result<(), String> {
        self.rebuild_hotkeys();
        self.ensure_defaults();
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let pretty = serde_json::to_string_pretty(&self.raw).map_err(|e| e.to_string())?;
        fs::write(path, pretty).map_err(|e| e.to_string())?;
        Ok(())
    }
}

fn read_log_tail(path: &Path, max_bytes: usize) -> Result<String, String> {
    let mut file = OpenOptions::new()
        .read(true)
        .open(path)
        .map_err(|e| format!("Failed to open {:?}: {}", path, e))?;
    let size = file.metadata().map_err(|e| e.to_string())?.len();
    let start = if size > max_bytes as u64 {
        size - max_bytes as u64
    } else {
        0
    };
    file.seek(SeekFrom::Start(start))
        .map_err(|e| e.to_string())?;
    let mut buffer = String::new();
    file.read_to_string(&mut buffer)
        .map_err(|e| e.to_string())?;
    Ok(buffer)
}

#[tauri::command]
fn set_manual_game_path(
    app: AppHandle,
    state: State<AppState>,
    path: Option<String>,
) -> Result<TrainerStatus, String> {
    let mut config = TrainerConfig::load(&state.paths.trainer_config);
    if let Some(ref p_str) = path {
        let p = Path::new(p_str);
        if !has_game_exe(p) {
            return Err(
                "Selected folder does not contain Watch_Dogs.exe or bin\\Watch_Dogs.exe"
                    .to_string(),
            );
        }
    }
    config.set_manual_game_path(path);
    if let Err(e) = config.save(&state.paths.trainer_config) {
        return Err(e);
    }

    let mut guard = state
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;
    let (platform, game_path) = detect_game_install(config.manual_game_path());
    guard.status.game_platform = platform;
    guard.status.game_path = game_path;
    guard.status.timestamp = Utc::now().to_rfc3339();
    guard.last_serialized = None;
    let payload = guard.status.clone();
    drop(guard);
    let _ = app.emit("status-update", payload.clone());
    Ok(payload)
}

#[tauri::command]
fn get_status(state: State<AppState>) -> Result<TrainerStatus, String> {
    let guard = state
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;
    Ok(guard.status.clone())
}

#[tauri::command]
fn read_loader_log(state: State<AppState>) -> Result<String, String> {
    read_log_tail(&state.paths.loader_log, 32 * 1024)
}

#[tauri::command]
fn read_trainer_log(state: State<AppState>) -> Result<String, String> {
    read_log_tail(&state.paths.trainer_log, 32 * 1024)
}

#[tauri::command]
fn open_release_folder(app: AppHandle, state: State<AppState>) -> Result<(), String> {
    let path = state.paths.release_dir.to_string_lossy().to_string();
    app.opener()
        .open_path(path, None::<String>)
        .map_err(|e| e.to_string())
}

#[tauri::command]
fn exit_app(app: AppHandle) {
    app.exit(0);
}

#[tauri::command]
fn activate_all(app: AppHandle, state: State<AppState>) -> Result<TrainerStatus, String> {
    {
        let guard = state
            .inner
            .lock()
            .map_err(|_| "State poisoned".to_string())?;
        if guard.status.diagnostic_mode {
            drop(guard);
            let mut guard = state
                .inner
                .lock()
                .map_err(|_| "State poisoned".to_string())?;
            guard.status.status_message = Some("Cheats disabled in diagnostic mode.".into());
            guard.status.last_error = Some("Cheats disabled in diagnostic mode.".into());
            guard.last_serialized = None;
            let payload = guard.status.clone();
            drop(guard);
            let _ = app.emit("status-update", payload.clone());
            return Ok(payload);
        }
    }

    let mut config = TrainerConfig::load(&state.paths.trainer_config);
    config.set_all(true);
    config.save(&state.paths.trainer_config)?;

    let mut guard = state
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;
    guard.status.cheats = build_cheat_entries(&config, guard.status.diagnostic_mode);
    guard.status.status_message = Some("All cheats activated.".into());
    guard.status.last_error = None;
    guard.status.timestamp = Utc::now().to_rfc3339();
    guard.last_serialized = None;
    let payload = guard.status.clone();
    drop(guard);
    let _ = app.emit("status-update", payload.clone());
    Ok(payload)
}

#[tauri::command]
fn deactivate_all(app: AppHandle, state: State<AppState>) -> Result<TrainerStatus, String> {
    let mut config = TrainerConfig::load(&state.paths.trainer_config);
    config.set_all(false);
    config.save(&state.paths.trainer_config)?;

    let mut guard = state
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;
    guard.status.cheats = build_cheat_entries(&config, guard.status.diagnostic_mode);
    guard.status.status_message = Some("All cheats deactivated.".into());
    guard.status.last_error = None;
    guard.status.timestamp = Utc::now().to_rfc3339();
    guard.last_serialized = None;
    let payload = guard.status.clone();
    drop(guard);
    let _ = app.emit("status-update", payload.clone());
    Ok(payload)
}

#[tauri::command]
fn set_cheat_state(
    app: AppHandle,
    state: State<AppState>,
    cheat_id: String,
    enabled: bool,
) -> Result<TrainerStatus, String> {
    {
        let guard = state
            .inner
            .lock()
            .map_err(|_| "State poisoned".to_string())?;
        if guard.status.diagnostic_mode && enabled {
            drop(guard);
            let mut guard = state
                .inner
                .lock()
                .map_err(|_| "State poisoned".to_string())?;
            guard.status.status_message = Some("Cheats disabled in diagnostic mode.".into());
            guard.status.last_error = Some("Cheats disabled in diagnostic mode.".into());
            guard.last_serialized = None;
            let payload = guard.status.clone();
            drop(guard);
            let _ = app.emit("status-update", payload.clone());
            return Ok(payload);
        }
    }

    let mut config = TrainerConfig::load(&state.paths.trainer_config);
    config.set_cheat(&cheat_id, enabled);
    config.save(&state.paths.trainer_config)?;

    let mut guard = state
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;
    guard.status.cheats = build_cheat_entries(&config, guard.status.diagnostic_mode);
    guard.status.status_message = Some(format!(
        "{} set to {}.",
        cheat_id,
        if enabled { "ON" } else { "OFF" }
    ));
    guard.status.last_error = None;
    guard.status.timestamp = Utc::now().to_rfc3339();
    guard.last_serialized = None;
    let payload = guard.status.clone();
    drop(guard);
    let _ = app.emit("status-update", payload.clone());
    Ok(payload)
}

#[tauri::command]
fn set_cheat_hotkey(
    app: AppHandle,
    state: State<AppState>,
    cheat_name: String,
    hotkey_vk: u32,
) -> Result<TrainerStatus, String> {
    let mut config = TrainerConfig::load(&state.paths.trainer_config);
    config.set_hotkey(&cheat_name, Some(hotkey_vk));
    config.save(&state.paths.trainer_config)?;

    let mut guard = state
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;
    guard.status.cheats = build_cheat_entries(&config, guard.status.diagnostic_mode);
    guard.status.status_message = Some(format!("Hotkey set for {}.", cheat_name));
    guard.status.timestamp = Utc::now().to_rfc3339();
    guard.last_serialized = None;
    let payload = guard.status.clone();
    drop(guard);
    let _ = app.emit("status-update", payload.clone());
    Ok(payload)
}

#[tauri::command]
fn clear_cheat_hotkey(
    app: AppHandle,
    state: State<AppState>,
    cheat_name: String,
) -> Result<TrainerStatus, String> {
    let mut config = TrainerConfig::load(&state.paths.trainer_config);
    config.set_hotkey(&cheat_name, None);
    config.save(&state.paths.trainer_config)?;

    let mut guard = state
        .inner
        .lock()
        .map_err(|_| "State poisoned".to_string())?;
    guard.status.cheats = build_cheat_entries(&config, guard.status.diagnostic_mode);
    guard.status.status_message = Some(format!("Hotkey cleared for {}.", cheat_name));
    guard.status.timestamp = Utc::now().to_rfc3339();
    guard.last_serialized = None;
    let payload = guard.status.clone();
    drop(guard);
    let _ = app.emit("status-update", payload.clone());
    Ok(payload)
}
