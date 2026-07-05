const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;

const STAGE_LABELS = {
  NOT_LOADED: "Not loaded",
  WAITING: "Waiting",
  INJECTING: "Injecting",
  VERIFYING: "Verifying",
  DIAGNOSTIC_LOADED: "Diagnostic Loaded",
  FAILED: "Failed",
};

const HEADER_STATUS = {
  NOT_LOADED: "Trainer not loaded",
  WAITING: "Waiting to inject",
  INJECTING: "Injecting trainer...",
  VERIFYING: "Verifying diagnostic load",
  DIAGNOSTIC_LOADED: "Diagnostic loaded",
  FAILED: "Trainer failed",
};

let statusState = null;
let capturing = null;
let unlistenStatus = null;
let visibleErrorMessages = [];

const els = {};

const EMPTY_LOG_TEXT = "No errors.";

const SUPPRESSED_LOG_PATTERNS = [
  /loader started/i,
  /\bdll path\b/i,
  /\barchitecture\b/i,
  /diagnostics? reloaded/i,
  /diagnosticmode\s*=\s*true/i,
  /\bnormal scanning\b/i,
  /\bscann?ing\b/i,
  /normal config reload/i,
  /config reload/i,
  /watch dogs.*waiting/i,
  /waiting.*watch dogs/i,
  /watch_dogs\.exe.*waiting/i,
];

const VISIBLE_ERROR_PATTERNS = [
  /injection failed/i,
  /access denied/i,
  /\bdll\b.*missing/i,
  /watchdogstrainer\.dll.*missing/i,
  /watch_dogs\.exe.*crash/i,
  /crashed after injection/i,
  /failed to read config/i,
  /backend command failed/i,
  /diagnostic dll caused watch dogs to close/i,
  /trainer_debug\.json.*(missing|invalid)/i,
  /\b(failed|error|denied|missing|invalid|crash|unable to|could not)\b/i,
];

function q(selector) {
  const el = document.querySelector(selector);
  if (!el) throw new Error(`Missing element: ${selector}`);
  return el;
}

function cacheDom() {
  els.processName = q("[data-process-name]");
  els.gameStatus = q("[data-game-status]");
  els.processId = q("[data-process-id]");
  els.trainerStatus = q("[data-trainer-status]");
  els.trainerStatusText = q("[data-trainer-status-text]");
  els.diagMode = q("[data-diag-mode]");
  els.hintText = q("[data-hint-text]");
  els.cheatsSummary = q("[data-cheats-summary]");
  els.activateAll = q('[data-action="activate-all"]');
  els.deactivateAll = q('[data-action="deactivate-all"]');
  els.openRelease = q('[data-action="open-release"]');
  els.exitApp = q('[data-action="exit"]');
  els.refreshLogs = q('[data-action="refresh-logs"]');
  els.logOutput = q("[data-log-output]");
  els.cheatTableBody = q("[data-cheat-table-body]");
}

function normalise(value) {
  if (typeof value === "string" && value.trim() === "") return null;
  return value ?? null;
}

function renderStatus(status, options = {}) {
  statusState = status;

  els.processName.textContent = status.process_name;
  els.processId.textContent = status.process_id ?? 0;

  const gameOn = Boolean(status.game_on);
  els.gameStatus.textContent = gameOn ? "Game Is ON" : "Game Is OFF";
  els.gameStatus.dataset.state = gameOn ? "on" : "off";

  const stageLabel = STAGE_LABELS[status.trainer_stage] ?? status.trainer_stage;
  els.trainerStatus.textContent = stageLabel;

  if (status.trainer_stage === "DIAGNOSTIC_LOADED") {
    els.trainerStatus.dataset.state = "on";
  } else if (status.trainer_stage === "FAILED") {
    els.trainerStatus.dataset.state = "off";
  } else if (status.trainer_stage === "WAITING" || status.trainer_stage === "INJECTING" || status.trainer_stage === "VERIFYING") {
    els.trainerStatus.dataset.state = "cyan";
  } else {
    delete els.trainerStatus.dataset.state;
  }

  const headerText = HEADER_STATUS[status.trainer_stage] ?? "Trainer not loaded";
  els.trainerStatusText.textContent = headerText;
  els.trainerStatusText.dataset.stage = status.trainer_stage;

  els.diagMode.textContent = status.diagnostic_mode ? "On" : "Off";
  els.diagMode.dataset.state = status.diagnostic_mode ? "cyan" : "off";

  const visibleLastError = isVisibleErrorMessage(status.last_error);
  if (visibleLastError) {
    els.hintText.textContent = status.last_error;
    els.hintText.style.color = "var(--red)";
    if (!options.suppressVisibleLog) addVisibleError(status.last_error);
  } else if (!gameOn) {
    els.hintText.textContent = "Launch Watch Dogs from Steam, load into Story Mode, then open this trainer.";
    els.hintText.style.color = "var(--orange)";
  } else {
    els.hintText.textContent = "";
  }

  const enabledCount = status.cheats.filter((c) => c.enabled).length;
  if (status.diagnostic_mode) {
    els.cheatsSummary.textContent = status.cheats_disabled_reason ?? "Cheats disabled in diagnostic mode.";
  } else {
    els.cheatsSummary.textContent = `${enabledCount} of ${status.cheats.length} cheats active`;
  }

  els.activateAll.disabled = Boolean(status.diagnostic_mode);
  els.activateAll.textContent = status.diagnostic_mode ? "Cheats disabled in diagnostic mode." : "Activate All";

  renderCheatTable(status);
}

function renderCheatTable(status) {
  els.cheatTableBody.innerHTML = "";
  if (!status || !Array.isArray(status.cheats)) return;

  let lastCat = null;

  status.cheats.forEach((cheat) => {
    if (cheat.category !== lastCat) {
      const catEl = document.createElement("div");
      catEl.className = "cat-header";
      catEl.textContent = cheat.category;
      els.cheatTableBody.appendChild(catEl);
      lastCat = cheat.category;
    }

    const row = document.createElement("div");
    row.className = "cheat-row";
    if (capturing === cheat.name) row.classList.add("is-capturing");
    if (cheat.enabled && !cheat.locked) row.classList.add("is-on");

    const hkCell = document.createElement("div");
    hkCell.className = "hk-cell";

    if (cheat.hotkey_vk && cheat.hotkey_vk > 0) {
      const keyEl = document.createElement("button");
      keyEl.className = "hk-key";
      keyEl.textContent = cheat.hotkey_label ?? `VK_${cheat.hotkey_vk}`;
      keyEl.addEventListener("click", () => beginCapture(cheat.name));
      hkCell.appendChild(keyEl);

      const clrEl = document.createElement("button");
      clrEl.className = "hk-clear";
      clrEl.textContent = "x";
      clrEl.addEventListener("click", (e) => { e.stopPropagation(); clearHotkey(cheat.name); });
      hkCell.appendChild(clrEl);
    } else {
      const bindEl = document.createElement("button");
      bindEl.className = "hk-bind";
      bindEl.textContent = capturing === cheat.name ? "Press key..." : "+ BIND";
      bindEl.addEventListener("click", () => beginCapture(cheat.name));
      hkCell.appendChild(bindEl);
    }

    const nameCell = document.createElement("div");
    nameCell.className = "cheat-name";
    if (capturing === cheat.name) nameCell.classList.add("is-capturing");
    nameCell.textContent = cheat.name;

    const stateCell = document.createElement("div");
    stateCell.className = "state-cell";

    const stateBtn = document.createElement("button");
    stateBtn.className = "state-btn";

    const trainerStage = status.trainer_stage;
    let stateText = "OFF";
    let stateFlag = "off";

    if (cheat.locked || status.diagnostic_mode) {
      stateText = status.diagnostic_mode ? "DISABLED" : "OFF";
      stateFlag = status.diagnostic_mode ? "disabled" : "off";
    } else if (cheat.enabled) {
      if (trainerStage === "DIAGNOSTIC_LOADED") {
        stateText = "ON";
        stateFlag = "on";
      } else if (trainerStage === "VERIFYING") {
        stateText = "VERIFYING";
        stateFlag = "verifying";
      } else if (trainerStage === "INJECTING" || trainerStage === "WAITING") {
        stateText = "QUEUED";
        stateFlag = "queued";
      } else {
        stateText = "QUEUED";
        stateFlag = "queued";
      }
    }

    stateBtn.dataset.state = stateFlag;
    stateBtn.textContent = stateText;
    stateBtn.disabled = cheat.locked || status.diagnostic_mode;
    stateBtn.addEventListener("click", () => toggleCheat(cheat.id, !cheat.enabled));
    stateCell.appendChild(stateBtn);

    row.append(hkCell, nameCell, stateCell);
    els.cheatTableBody.appendChild(row);
  });

  if (capturing) {
    const banner = document.createElement("div");
    banner.className = "capture-banner";
    banner.textContent = `  Press a key to bind:  ${capturing}     (ESC = cancel  |  Del = clear)`;
    els.cheatTableBody.appendChild(banner);
  }
}

function beginCapture(cheatName) {
  if (capturing === cheatName) {
    cancelCapture();
    return;
  }
  capturing = cheatName;
  if (statusState) renderCheatTable(statusState);
}

function cancelCapture() {
  capturing = null;
  if (statusState) renderCheatTable(statusState);
}

async function clearHotkey(cheatName) {
  try {
    const status = await invoke("clear_cheat_hotkey", { cheat_name: cheatName });
    renderStatus(status);
  } catch (err) {
    console.error("Failed to clear hotkey", err);
  }
}

async function handleCapturedKey(event) {
  if (!capturing) return;
  event.preventDefault();
  event.stopPropagation();

  const cheatName = capturing;
  const vk = event.keyCode || event.which;

  if (event.key === "Escape") {
    cancelCapture();
    return;
  }

  if (event.key === "Delete") {
    try {
      const status = await invoke("clear_cheat_hotkey", { cheat_name: cheatName });
      renderStatus(status);
    } catch (err) {
      console.error("Failed to clear hotkey", err);
    } finally {
      cancelCapture();
    }
    return;
  }

  if (!vk) return;

  try {
    const status = await invoke("set_cheat_hotkey", { cheat_name: cheatName, hotkey_vk: vk });
    renderStatus(status);
  } catch (err) {
    console.error("Failed to set hotkey", err);
  } finally {
    cancelCapture();
  }
}

async function toggleCheat(cheatId, nextState) {
  try {
    const status = await invoke("set_cheat_state", { cheat_id: cheatId, enabled: nextState });
    renderStatus(status);
  } catch (err) {
    console.error("Failed to toggle cheat", err);
  }
}

async function activateAllCheats() {
  if (!statusState || statusState.diagnostic_mode) return;
  try {
    const status = await invoke("activate_all");
    renderStatus(status);
  } catch (err) {
    console.error("Failed to activate all", err);
  }
}

async function deactivateAllCheats() {
  try {
    const status = await invoke("deactivate_all");
    renderStatus(status);
  } catch (err) {
    console.error("Failed to deactivate all", err);
  }
}

async function openReleaseFolder() {
  try {
    await invoke("open_release_folder");
  } catch (err) {
    console.error("Failed to open release folder", err);
  }
}

function exitApp() {
  invoke("exit_app").catch((err) => console.error("Failed to exit app", err));
}

function prettyError(err) {
  if (err instanceof Error) return err.message;
  return String(err ?? "Unknown error");
}

function isVisibleErrorMessage(value) {
  const line = String(value ?? "").trim();
  if (!line) return false;
  if (SUPPRESSED_LOG_PATTERNS.some((pattern) => pattern.test(line))) return false;
  return VISIBLE_ERROR_PATTERNS.some((pattern) => pattern.test(line));
}

function uniqueMessages(messages) {
  const seen = new Set();
  const result = [];

  messages.forEach((message) => {
    const text = String(message ?? "").trim();
    if (!text || seen.has(text)) return;
    seen.add(text);
    result.push(text);
  });

  return result;
}

function filterVisibleErrors(logText) {
  return uniqueMessages(
    String(logText ?? "")
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter(isVisibleErrorMessage),
  );
}

function renderVisibleErrors(messages = visibleErrorMessages) {
  const errors = uniqueMessages(messages);
  visibleErrorMessages = errors;

  if (errors.length === 0) {
    els.logOutput.textContent = EMPTY_LOG_TEXT;
    els.logOutput.dataset.empty = "true";
    return;
  }

  els.logOutput.textContent = errors.join("\n");
  delete els.logOutput.dataset.empty;
}

function addVisibleError(message) {
  if (!isVisibleErrorMessage(message)) return;
  renderVisibleErrors([...visibleErrorMessages, message]);
}

async function loadLogs() {
  els.logOutput.textContent = "Checking errors...";
  els.logOutput.dataset.empty = "true";

  const results = await Promise.allSettled([
    invoke("read_loader_log"),
    invoke("read_trainer_log"),
  ]);

  const logErrors = results.flatMap((result) => {
    if (result.status === "fulfilled") return filterVisibleErrors(result.value);
    return [`Backend command failed: ${prettyError(result.reason)}`];
  });

  renderVisibleErrors([...visibleErrorMessages, ...logErrors]);
}

async function refreshStatus(options = {}) {
  try {
    const status = await invoke("get_status");
    renderStatus(status, options);
  } catch (err) {
    console.error("Failed to fetch status", err);
  }
}

function attachEvents() {
  els.activateAll.addEventListener("click", activateAllCheats);
  els.deactivateAll.addEventListener("click", deactivateAllCheats);
  els.openRelease.addEventListener("click", openReleaseFolder);
  els.exitApp.addEventListener("click", exitApp);
  els.refreshLogs.addEventListener("click", loadLogs);

  document.addEventListener("keydown", handleCapturedKey, true);
}

async function initialiseStatusListener() {
  if (unlistenStatus) await unlistenStatus();
  unlistenStatus = await listen("status-update", (event) => {
    if (event?.payload) renderStatus(event.payload);
  });
}

async function init() {
  cacheDom();
  attachEvents();
  renderVisibleErrors();
  await Promise.all([refreshStatus({ suppressVisibleLog: true }), initialiseStatusListener()]);
}

window.addEventListener("DOMContentLoaded", init);
