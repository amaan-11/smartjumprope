// app/private/data_base.js
const sqlite3 = require("sqlite3").verbose();
const path = require("path");

const DB_PATH = path.join(__dirname, "database.db");

const db = new sqlite3.Database(DB_PATH, (err) => {
    if (err) {
        console.error("Failed to open SQLite DB:", err);
        process.exit(1);
    }
    console.log("SQLite DB opened:", DB_PATH);

    db.run("PRAGMA foreign_keys = ON;");
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

function get(sql, params = []) {
    return new Promise((resolve, reject) => {
        db.get(sql, params, (err, row) => {
            if (err) return reject(err);
            resolve(row);
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

function requireIntPositive(x, fieldName) {
    const n = Number(x);
    if (!Number.isFinite(n) || n <= 0) {
        throw new Error(`${fieldName} must be a positive number`);
    }
    return Math.trunc(n);
}

function requireNonEmptyString(x, fieldName) {
    if (typeof x !== "string" || !x.trim()) {
        throw new Error(`${fieldName} must be a non-empty string`);
    }
    return x.trim();
}

const USERNAME_MIN_LEN = 4;
const USERNAME_MAX_LEN = 20;

function requireUsername(x) {
    if (typeof x !== "string") throw new Error("username must be a string");
    const s = x.trim();
    if (s.length < USERNAME_MIN_LEN || s.length > USERNAME_MAX_LEN) {
        throw new Error(`username length must be ${USERNAME_MIN_LEN}-${USERNAME_MAX_LEN} characters`);
    }
    return s;
}

// user_id: strictly 6 digits
function requireUserId6Digits(x) {
    if (typeof x !== "string") throw new Error("user_id must be a string");
    const s = x.trim();
    if (!/^\d{6}$/.test(s)) {
        throw new Error("user_id must be 6 digits");
    }
    return s;
}

// --- public API ---
async function init() {
    await run(`
        CREATE TABLE IF NOT EXISTS users (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

          username TEXT NOT NULL UNIQUE,
          user_id   TEXT NOT NULL
        );
        
    `);

    // After CREATE TABLE users ...

    // user_id must also be unique
    await run(`CREATE UNIQUE INDEX IF NOT EXISTS ux_users_user_id ON users(user_id);`);


    await run(`CREATE UNIQUE INDEX IF NOT EXISTS ux_users_username_user_id ON users(username, user_id);`);

    // Create workouts (as before)
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

    // IMPORTANT: add the user_id column if it doesn't exist yet
    const cols = await all(`PRAGMA table_info(workouts);`);
    const hasUserId = cols.some((c) => c.name === "user_id");
    if (!hasUserId) {
        await run(`ALTER TABLE workouts ADD COLUMN user_id INTEGER;`);
    }

    await run(`CREATE INDEX IF NOT EXISTS idx_workouts_user_start ON workouts(user_id, start_time);`);

    console.log("DB ready: users + workouts tables ensured.");
}

async function createUser({ username, user_id }) {
    const u = requireUsername(username);
    const id = requireUserId6Digits(user_id);


    const exists = await get(
        `SELECT id, username, user_id FROM users WHERE username = ? OR user_id = ?`,
        [u, id]
    );

    if (exists) {
        if (exists.username === u && exists.user_id === id) {
            throw new Error("User with the same username and user_id already exists");
        }
        if (exists.username === u) {
            throw new Error("Username already exists");
        }
        if (exists.user_id === id) {
            throw new Error("user_id already exists");
        }
        throw new Error("User already exists");
    }

    try {
        const result = await run(
            `INSERT INTO users (username, user_id) VALUES (?, ?)`,
            [u, id]
        );
        return get(
            `SELECT id, username, user_id, created_at FROM users WHERE id = ?`,
            [result.lastID]
        );
    } catch (err) {
        // Final safety net: if two requests arrive at the same time, UNIQUE in the DB will trigger here
        const msg = String(err && err.message ? err.message : err);

        if (msg.includes("users.username")) {
            throw new Error("Username already exists");
        }
        if (msg.includes("users.user_id")) {
            throw new Error("user_id already exists");
        }
        if (msg.includes("ux_users_username_user_id")) {
            throw new Error("User with the same username and user_id already exists");
        }
        if (msg.includes("SQLITE_CONSTRAINT")) {
            throw new Error("User already exists");
        }
        throw err;
    }
}

async function findUserByCredentials(username, user_id) {
    const u = requireUsername(username);
    const id = requireUserId6Digits(user_id);

    return get(
        `SELECT id, username, user_id, created_at FROM users WHERE username = ? AND user_id = ?`,
        [u, id]
    );
}

// --- workouts are linked to the user ---
async function insertWorkoutForUser(userId, body) {
    const uid = requireIntPositive(userId, "userId");

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
          (user_id, start_time, end_time, duration_ms, jump_count, avg_heart_rate_bpm, max_heart_rate_bpm, device_name)
        VALUES
          (?, ?, ?, ?, ?, ?, ?, ?)
        `,
        [uid, body.start_time, body.end_time, duration_ms, jump_count, avg_hr, max_hr, device_name]
    );

    return get("SELECT * FROM workouts WHERE id = ?", [result.lastID]);
}

async function listWorkoutsForUser(userId, limit = 20) {
    const uid = requireIntPositive(userId, "userId");
    const n = Math.min(Math.max(Number(limit || 20), 1), 100);

    return all(
        "SELECT * FROM workouts WHERE user_id = ? ORDER BY datetime(start_time) DESC LIMIT ?",
        [uid, n]
    );
}

module.exports = {
    init,

    createUser,
    findUserByCredentials,

    insertWorkoutForUser,
    listWorkoutsForUser,
};