// app/public/ble.js
// Web Bluetooth (Browser Central) for Smart Jump Rope.
// Goal: connect to ESP32, start/stop streaming, and save a workout summary to the backend.
//
// Packet contract (12 bytes, little-endian):
// u32 timestamp_ms (0)
// u32 jump_count   (4)
// u8  heart_rate   (8)
// u16 accel_mag    (9)
// u8  flags        (11)

const JR_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const JR_CTRL_UUID    = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const JR_DATA_UUID    = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

// Simple debug (demo)
const DEBUG_BLE = true;
const DEBUG_LOG_INTERVAL_MS = 600;
let _lastDebugLog = 0;

function dbg(...args) {
    if (!DEBUG_BLE) return;
    const now = Date.now();
    if (now - _lastDebugLog < DEBUG_LOG_INTERVAL_MS) return;
    _lastDebugLog = now;
    console.log(...args);
}

let device, server, ctrlChar, dataChar;

// Workout state (summary only; we do not store raw telemetry in DB)
const workout = {
    active: false,
    startMs: 0,

    // Jump tracking:
    lastJumpRaw: 0,
    lastJumpCount: 0,

    // Baseline fallback (in case firmware sends cumulative count)
    preStartJumpRaw: null,
    baselineMode: "unknown", // "cumulative" | "firmware_reset" | "unknown"
    jumpBaseline: 0,

    // HR tracking:
    hrSamples: [],
    maxHr: 0,

    // Device name stored in DB
    deviceName: "JRope-C6",
};

function nowMs() { return Date.now(); }
function isoFromMs(ms) { return new Date(ms).toISOString(); }

function averageInt(arr) {
    if (!arr.length) return null;
    const sum = arr.reduce((a, b) => a + b, 0);
    return Math.round(sum / arr.length);
}

function setStatus(msg) {
    const el = document.getElementById("bleStatus");
    if (el) el.textContent = msg;
}

function enableControls(connected) {
    const start = document.getElementById("btnStart");
    const stop = document.getElementById("btnStop");
    if (start) start.disabled = !connected;
    if (stop) stop.disabled = !connected;
}

function setLiveUI({ jumps, hr, accelMag }) {
    const jumpsEl = document.getElementById("jumpCount");
    const hrEl = document.getElementById("heartRate");
    const accelEl = document.getElementById("accelMag");

    if (jumpsEl) jumpsEl.textContent = String(jumps);
    if (hrEl) hrEl.textContent = (hr === 0 ? "-" : String(hr));
    if (accelEl) accelEl.textContent = String(accelMag);
}

// Auto-baseline detection (only needed if firmware does NOT reset transmitted jump_count on Start).
function resolveBaselineIfNeeded(jumpRaw) {
    if (workout.baselineMode !== "unknown") return;

    if (workout.preStartJumpRaw == null) {
        workout.baselineMode = "unknown";
        workout.jumpBaseline = 0;
        return;
    }

    if (jumpRaw >= workout.preStartJumpRaw) {
        workout.baselineMode = "cumulative";
        workout.jumpBaseline = workout.preStartJumpRaw;
    } else {
        workout.baselineMode = "firmware_reset";
        workout.jumpBaseline = 0;
    }

    dbg("[BLE] Baseline resolved:", {
        baselineMode: workout.baselineMode,
        preStartJumpRaw: workout.preStartJumpRaw,
        jumpBaseline: workout.jumpBaseline,
    });
}

function jumpRelative(jumpRaw) {
    resolveBaselineIfNeeded(jumpRaw);

    const base = workout.jumpBaseline || 0;
    if (jumpRaw >= base) return (jumpRaw - base);

    // Avoid underflow if baseline is wrong for any reason
    return jumpRaw;
}

async function connectBLE() {
    if (!navigator.bluetooth) {
        setStatus("Web Bluetooth is not supported in this browser.");
        return;
    }

    setStatus("Searching for device...");
    device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [JR_SERVICE_UUID] }],
    });

    workout.deviceName = (device && device.name) ? device.name : workout.deviceName;

    device.addEventListener("gattserverdisconnected", () => {
        setStatus("Disconnected");
        enableControls(false);
    });

    setStatus("Connecting...");
    server = await device.gatt.connect();

    const service = await server.getPrimaryService(JR_SERVICE_UUID);
    ctrlChar = await service.getCharacteristic(JR_CTRL_UUID);
    dataChar = await service.getCharacteristic(JR_DATA_UUID);

    await dataChar.startNotifications();
    dataChar.addEventListener("characteristicvaluechanged", onData);

    setStatus(`Connected: ${workout.deviceName}`);
    enableControls(true);

    dbg("[BLE] Connected", { deviceName: workout.deviceName });
}

function onData(event) {
    const v = event.target.value; // DataView
    const len = v.byteLength;

    // Safety: avoid DataView RangeError if firmware sends unexpected payload size.
    if (len !== 12) {
        console.warn("[BLE] Unexpected packet size:", len, "(expected 12). Ignoring.");
        return;
    }

    // Parse little-endian (true)
    const ts = v.getUint32(0, true);
    const jumpRaw = v.getUint32(4, true);
    const hr = v.getUint8(8);
    const accelMag = v.getUint16(9, true);
    const flags = v.getUint8(11);

    workout.lastJumpRaw = jumpRaw;

    const jumpsForUi = workout.active ? jumpRelative(jumpRaw) : jumpRaw;

    setLiveUI({ jumps: jumpsForUi, hr, accelMag });

    workout.lastJumpCount = jumpsForUi;

    // Ignore hr==0 (invalid/no reading)
    if (workout.active && typeof hr === "number" && hr > 0) {
        workout.hrSamples.push(hr);
        if (hr > workout.maxHr) workout.maxHr = hr;
    }

    dbg("[BLE] pkt", { len, ts, jumpRaw, jumpsForUi, hr, accelMag, flags });
}

async function startStreaming() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x01]));
}

async function stopStreaming() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x00]));
}

function startWorkout() {
    workout.active = true;
    workout.startMs = nowMs();

    workout.preStartJumpRaw = (typeof workout.lastJumpRaw === "number") ? workout.lastJumpRaw : null;
    workout.baselineMode = "unknown";
    workout.jumpBaseline = 0;

    workout.hrSamples = [];
    workout.maxHr = 0;

    workout.lastJumpCount = 0;

    dbg("[WORKOUT] Start", {
        startMs: workout.startMs,
        preStartJumpRaw: workout.preStartJumpRaw,
    });
}

async function saveWorkoutToDb() {
    const endMs = nowMs();
    const durationMs = endMs - workout.startMs;

    const avgHr = averageInt(workout.hrSamples);
    const maxHr = workout.hrSamples.length ? workout.maxHr : null;

    const payload = {
        start_time: isoFromMs(workout.startMs),
        end_time: isoFromMs(endMs),
        duration_ms: durationMs,
        jump_count: workout.lastJumpCount,
        avg_heart_rate_bpm: avgHr,
        max_heart_rate_bpm: maxHr,
        device_name: workout.deviceName,
    };

    dbg("[WORKOUT] Saving", payload);

    const res = await fetch("/api/workouts", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
    });

    const json = await res.json().catch(() => ({}));
    if (!res.ok || !json.ok) {
        throw new Error(json.error || `HTTP ${res.status}`);
    }

    return json.workout;
}

async function handleStartClick() {
    try {
        startWorkout();
        await startStreaming();
        setStatus("Streaming: ON");
    } catch (e) {
        workout.active = false;
        setStatus(String(e));
    }
}

async function handleStopClick() {
    try {
        const hadWorkout = (workout.startMs !== 0);

        workout.active = false;

        await stopStreaming();
        setStatus("Streaming: OFF");

        if (hadWorkout) {
            await saveWorkoutToDb();
        }

        if (typeof window.renderHistory === "function") {
            window.renderHistory().catch(() => {});
        }

        setLiveUI({ jumps: 0, hr: 0, accelMag: 0 });
    } catch (e) {
        setStatus(String(e));
    }
}

function initBLE() {
    const btnConnect = document.getElementById("btnConnect");
    const btnStart = document.getElementById("btnStart");
    const btnStop = document.getElementById("btnStop");

    if (btnConnect) btnConnect.addEventListener("click", () => connectBLE().catch(e => setStatus(String(e))));
    if (btnStart) btnStart.addEventListener("click", () => handleStartClick());
    if (btnStop) btnStop.addEventListener("click", () => handleStopClick());

    setStatus("Disconnected");
    enableControls(false);
}

document.addEventListener("DOMContentLoaded", initBLE);
