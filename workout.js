// During development: run Node locally (npm start in app/private)
const BACKEND_BASE_URL = "http://localhost:3000";

const workoutState = {
    active: false,
    startTimeMs: 0,
    lastJumpCount: 0,
    hrSamples: [],
    maxHr: 0,
    deviceName: "JRope-C6"
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

function setWorkoutStatus(msg) {
    const el = document.getElementById("workoutStatus");
    if (el) el.textContent = msg;
}

async function postWorkoutSummary(summary) {
    const res = await fetch(`${BACKEND_BASE_URL}/api/workouts`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(summary)
    });

    const json = await res.json().catch(() => ({}));
    if (!res.ok || !json.ok) {
        throw new Error(json.error || `HTTP ${res.status}`);
    }
    return json.workout;
}

function startWorkout() {
    workoutState.active = true;
    workoutState.startTimeMs = nowMs();
    workoutState.hrSamples = [];
    workoutState.maxHr = 0;
    setWorkoutStatus("Workout: running...");
}

async function stopWorkout() {
    if (!workoutState.active) return;

    workoutState.active = false;
    const endTimeMs = nowMs();
    const durationMs = endTimeMs - workoutState.startTimeMs;

    const avgHr = averageInt(workoutState.hrSamples);
    const maxHr = workoutState.hrSamples.length ? workoutState.maxHr : null;

    const payload = {
        start_time: isoFromMs(workoutState.startTimeMs),
        end_time: isoFromMs(endTimeMs),
        duration_ms: durationMs,
        jump_count: workoutState.lastJumpCount,
        avg_heart_rate_bpm: avgHr,
        max_heart_rate_bpm: maxHr,
        device_name: workoutState.deviceName
    };

    setWorkoutStatus("Workout: saving...");
    const inserted = await postWorkoutSummary(payload);
    setWorkoutStatus(`Workout: saved (id=${inserted.id})`);
}

function workoutOnLiveData({ jumps, hr }) {
    workoutState.lastJumpCount = jumps;

    if (workoutState.active && typeof hr === "number" && hr > 0) {
        workoutState.hrSamples.push(hr);
        if (hr > workoutState.maxHr) workoutState.maxHr = hr;
    }
}

async function checkBackend() {
    try {
        const res = await fetch(`${BACKEND_BASE_URL}/api/workouts?limit=1`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        setWorkoutStatus("Workout: idle (backend OK)");
    } catch (e) {
        setWorkoutStatus("Workout: idle (backend NOT reachable)");
    }
}

function initWorkoutTracking() {
    const btnStart = document.getElementById("btnStart");
    const btnStop = document.getElementById("btnStop");

    if (btnStart) btnStart.addEventListener("click", startWorkout);
    if (btnStop) btnStop.addEventListener("click", () => {
        stopWorkout().catch(err => setWorkoutStatus(`Workout: save failed: ${String(err)}`));
    });

    setWorkoutStatus("Workout: idle");
    checkBackend();
}

document.addEventListener("DOMContentLoaded", initWorkoutTracking);
window.workoutOnLiveData = workoutOnLiveData;
