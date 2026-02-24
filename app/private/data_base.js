// app/private/data_base.js
// SQLite database layer for workout session summaries

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

// --- small promise helpers ---
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

// --- validation helpers ---
function isISODateString(s) {
    return typeof s === "string" && !Number.isNaN(Date.parse(s));
}

function toIntOrNull(x) {
    if (x === null || x === undefined || x === "") return null;
    const n = Number(x);
    return Number.isFinite(n) ? Math.trunc(n) : null;
}

function requireIntNonNegative(x, fieldName) {
    const n = Number(x);
    if (!Number.isFinite(n) || n < 0) {
        throw new Error(`${fieldName} must be a non-negative number`);
    }
    return Math.trunc(n);
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

      avg_heart_rate_bpm INTEGER,
      max_heart_rate_bpm INTEGER,

      device_name TEXT NOT NULL DEFAULT 'JRope-C6'
    );
  `);

    console.log("DB ready: workouts table ensured.");
}

async function insertWorkout(body) {
    if (!body || typeof body !== "object") throw new Error("Body must be JSON");

    if (!isISODateString(body.start_time)) throw new Error("start_time must be an ISO date string");
    if (!isISODateString(body.end_time)) throw new Error("end_time must be an ISO date string");

    const duration_ms = requireIntNonNegative(body.duration_ms, "duration_ms");
    const jump_count = requireIntNonNegative(body.jump_count, "jump_count");

    const avg_hr = toIntOrNull(body.avg_heart_rate_bpm);
    const max_hr = toIntOrNull(body.max_heart_rate_bpm);

    const device_name =
        typeof body.device_name === "string" && body.device_name.trim()
            ? body.device_name.trim()
            : "JRope-C6";

    const result = await run(
        `
      INSERT INTO workouts
        (start_time, end_time, duration_ms, jump_count, avg_heart_rate_bpm, max_heart_rate_bpm, device_name)
      VALUES
        (?, ?, ?, ?, ?, ?, ?)
    `,
        [body.start_time, body.end_time, duration_ms, jump_count, avg_hr, max_hr, device_name]
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