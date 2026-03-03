// app/private/data_base.js
// SQLite layer for storing workout summaries (local demo).
//
// Important:
// - We store ONLY workout summaries (start/end/duration/jumps/HR).
// - We do NOT store raw telemetry streams in SQLite for this demo.
//
// Date notes:
// - Frontend sends ISO-8601 strings (new Date().toISOString()).
// - SQLite stores dates as TEXT; datetime() works well with ISO strings.

const sqlite3 = require("sqlite3").verbose();
const path = require("path");

const DB_PATH = path.join(__dirname, "database.db");

const db = new sqlite3.Database(DB_PATH, (err) => {
    if (err) {
        console.error("Failed to open SQLite DB:", err);
        process.exit(1);
    }
    console.log("SQLite DB opened:", DB_PATH);
});

// --- helpers (promises) ---
function run(sql, params = []) {
    return new Promise((resolve, reject) => {
        db.run(sql, params, function (err) {
            if (err) return reject(err);
            resolve({ lastID: this.lastID, changes: this.changes });
        });
    });
}

function all(sql, params = []) {
    return new Promise((resolve, reject) => {
        db.all(sql, params, (err, rows) => {
            if (err) return reject(err);
            resolve(rows);
        });
    });
}

// --- validation (demo) ---
function isISODateString(s) {
    return typeof s === "string" && s.trim() && !Number.isNaN(Date.parse(s));
}

function toIntOrNull(x) {
    if (x === null || x === undefined || x === "") return null;
    const n = Number(x);
    return Number.isFinite(n) ? Math.trunc(n) : null;
}

function requireIntNonNegative(x, fieldName) {
    const n = Number(x);
    if (!Number.isFinite(n) || n < 0) {
        throw new Error(`${fieldName} must be an integer >= 0`);
    }
    return Math.trunc(n);
}

function requireIntInRangeOrNull(x, fieldName, min, max) {
    const n = toIntOrNull(x);
    if (n === null) return null;
    if (n < min || n > max) {
        throw new Error(`${fieldName} must be between ${min} and ${max}`);
    }
    return n;
}

function clampDeviceName(name) {
    if (typeof name !== "string") return "JRope-C6";
    const s = name.trim();
    if (!s) return "JRope-C6";
    return s.length > 64 ? s.slice(0, 64) : s;
}

// --- public API ---
async function init() {
    await run(`
        CREATE TABLE IF NOT EXISTS workouts (
                                                id INTEGER PRIMARY KEY AUTOINCREMENT,
                                                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

                                                start_time TEXT NOT NULL,
                                                end_time   TEXT NOT NULL,
                                                duration_ms INTEGER NOT NULL CHECK(duration_ms >= 0),

            jump_count INTEGER NOT NULL CHECK(jump_count >= 0),

            avg_heart_rate_bpm INTEGER CHECK(avg_heart_rate_bpm IS NULL OR (avg_heart_rate_bpm BETWEEN 0 AND 255)),
            max_heart_rate_bpm INTEGER CHECK(max_heart_rate_bpm IS NULL OR (max_heart_rate_bpm BETWEEN 0 AND 255)),

            device_name TEXT NOT NULL DEFAULT 'JRope-C6'
            );
    `);

    console.log("DB ready: workouts table ensured.");
}

async function insertWorkout(body) {
    if (!body || typeof body !== "object" || Array.isArray(body)) {
        throw new Error("Body must be a JSON object");
    }

    const start_time = (typeof body.start_time === "string") ? body.start_time.trim() : "";
    const end_time = (typeof body.end_time === "string") ? body.end_time.trim() : "";

    if (!isISODateString(start_time)) throw new Error("start_time must be an ISO-8601 string");
    if (!isISODateString(end_time)) throw new Error("end_time must be an ISO-8601 string");

    const startMs = Date.parse(start_time);
    const endMs = Date.parse(end_time);
    if (endMs < startMs) throw new Error("end_time cannot be before start_time");

    const duration_ms = requireIntNonNegative(body.duration_ms, "duration_ms");
    const jump_count = requireIntNonNegative(body.jump_count, "jump_count");

    // Basic consistency: duration should match end-start within tolerance.
    const expected = endMs - startMs;
    const tolerance = 5000; // 5 seconds (demo)
    if (Math.abs(duration_ms - expected) > tolerance) {
        throw new Error("duration_ms does not match start_time/end_time");
    }

    // HR comes from u8 in BLE, so keep it 0..255.
    const avg_hr = requireIntInRangeOrNull(body.avg_heart_rate_bpm, "avg_heart_rate_bpm", 0, 255);
    const max_hr = requireIntInRangeOrNull(body.max_heart_rate_bpm, "max_heart_rate_bpm", 0, 255);

    if (avg_hr !== null && max_hr !== null && max_hr < avg_hr) {
        throw new Error("max_heart_rate_bpm must be >= avg_heart_rate_bpm");
    }

    const device_name = clampDeviceName(body.device_name);

    const result = await run(
        `
            INSERT INTO workouts
            (start_time, end_time, duration_ms, jump_count, avg_heart_rate_bpm, max_heart_rate_bpm, device_name)
            VALUES
            (?, ?, ?, ?, ?, ?, ?)
        `,
        [start_time, end_time, duration_ms, jump_count, avg_hr, max_hr, device_name]
    );

    const rows = await all("SELECT * FROM workouts WHERE id = ?", [result.lastID]);
    return rows[0];
}

async function listWorkouts(limit = 20) {
    const n = Math.min(Math.max(Number(limit || 20), 1), 100);
    return all("SELECT * FROM workouts ORDER BY datetime(start_time) DESC LIMIT ?", [n]);
}

module.exports = {
    init,
    insertWorkout,
    listWorkouts,
};