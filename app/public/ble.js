// ===== BLE UUIDs (choose once, match ESP32 firmware) =====
const JR_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const JR_CTRL_UUID    = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const JR_DATA_UUID    = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

let device, server, ctrlChar, dataChar;
let hrEnabled = false;

// ---- Simple workout state (no users) ----
const workout = {
    active: false,
    startMs: 0,
    lastJumpCount: 0,
    hrSamples: [],
    maxHr: 0,
    deviceName: "JRope-C6",
};

function nowMs() {
    return Date.now();
}

function isoFromMs(ms) {
    return new Date(ms).toISOString();
}

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
    const hrToggle = document.getElementById("btnToggleHR");
    if (start) start.disabled = !connected;
    if (stop) stop.disabled = !connected;
    if (hrToggle) hrToggle.disabled = !connected;
}

async function connectBLE() {
    if (!navigator.bluetooth) {
        setStatus("Web Bluetooth not supported in this browser.");
        return;
    }

    setStatus("Requesting device...");
    device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [JR_SERVICE_UUID] }],
    });

    device.addEventListener("gattserverdisconnected", () => {
        setStatus("Disconnected");
        enableControls(false);
        hrEnabled = false;
        setHrToggleLabel();
    });

    setStatus("Connecting...");
    server = await device.gatt.connect();

    const service = await server.getPrimaryService(JR_SERVICE_UUID);
    ctrlChar = await service.getCharacteristic(JR_CTRL_UUID);
    dataChar = await service.getCharacteristic(JR_DATA_UUID);

    await dataChar.startNotifications();
    dataChar.addEventListener("characteristicvaluechanged", onData);

    setStatus("Connected");
    enableControls(true);
}

function onData(event) {
    const v = event.target.value; // DataView

    const ts = v.getUint32(0, true);
    const jumps = v.getUint32(4, true);
    const hr = v.getUint8(8);
    const accelMag = v.getUint16(9, true);
    const flags = v.getUint8(11);

    // Update UI
    const jumpsEl = document.getElementById("jumpCount");
    const hrEl = document.getElementById("heartRate");
    const accelEl = document.getElementById("accelMag");

    if (jumpsEl) jumpsEl.textContent = String(jumps);
    if (hrEl) hrEl.textContent = hr === 0 ? "-" : String(hr);
    if (accelEl) accelEl.textContent = String(accelMag);

    // Track for saving later
    workout.lastJumpCount = jumps;
    if (workout.active && typeof hr === "number" && hr > 0) {
        workout.hrSamples.push(hr);
        if (hr > workout.maxHr) workout.maxHr = hr;
    }

    console.log({ ts, jumps, hr, accelMag, flags });
}

async function startStreaming() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x01]));
}

async function stopStreaming() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x00]));
}

async function startHr() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x02]));
}

async function stopHr() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x03]));
}

function setHrToggleLabel() {
    const btn = document.getElementById("btnToggleHR");
    if (!btn) return;
    btn.textContent = hrEnabled ? "Stop HR" : "Start HR";
}

function startWorkout() {
    workout.active = true;
    workout.startMs = nowMs();
    workout.hrSamples = [];
    workout.maxHr = 0;
    workout.lastJumpCount = 0;
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
    } catch (e) {
        setStatus(String(e));
    }
}

async function handleStopClick() {
    try {
        workout.active = false;

        await stopStreaming();

        // Save workout to DB
        await saveWorkoutToDb();

        // If history.js is present, refresh the table
        if (typeof window.renderHistory === "function") {
            window.renderHistory().catch(() => {});
        }

        // Optional: reset UI
        const jump_counts = document.getElementById("jumpCount");
        const hr = document.getElementById("heartRate");
        const acceleration = document.getElementById("accelMag");

        if (jump_counts) jump_counts.textContent = "0";
        if (hr) hr.textContent = "-";
        if (acceleration) acceleration.textContent = "0";
    } catch (e) {
        setStatus(String(e));
    }
}

async function handleHrToggleClick() {
    try {
        if (!ctrlChar) return;
        if (hrEnabled) {
            await stopHr();
            hrEnabled = false;
        } else {
            await startHr();
            hrEnabled = true;
        }
        setHrToggleLabel();
    } catch (e) {
        setStatus(String(e));
    }
}

function initBLE() {
    const btnConnect = document.getElementById("btnConnect");
    const btnStart = document.getElementById("btnStart");
    const btnStop = document.getElementById("btnStop");
    const btnToggleHR = document.getElementById("btnToggleHR");

    if (btnConnect) btnConnect.addEventListener("click", () => connectBLE().catch(e => setStatus(String(e))));
    if (btnStart) btnStart.addEventListener("click", () => handleStartClick());
    if (btnStop) btnStop.addEventListener("click", () => handleStopClick());
    if (btnToggleHR) btnToggleHR.addEventListener("click", () => handleHrToggleClick());

    setStatus("Disconnected");
    enableControls(false);
    setHrToggleLabel();
}

document.addEventListener("DOMContentLoaded", initBLE);
