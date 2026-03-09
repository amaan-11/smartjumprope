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
        const err = new Error(`${fieldName} must be a non-negative number`);
        err.status = 400;
        throw err;
    }
    return Math.trunc(n);
}

function requireIntPositive(x, fieldName) {
    const n = Number(x);
    if (!Number.isFinite(n) || n <= 0) {
        const err = new Error(`${fieldName} must be a positive number`);
        err.status = 400;
        throw err;
    }
    return Math.trunc(n);
}

function requireNonEmptyString(x, fieldName) {
    if (typeof x !== "string" || !x.trim()) {
        const err = new Error(`${fieldName} must be a non-empty string`);
        err.status = 400;
        throw err;
    }
    return x.trim();
}

const USERNAME_MIN_LEN = 4;
const USERNAME_MAX_LEN = 20;

function requireUsername(x) {
    if (typeof x !== "string") {
        const err = new Error("username must be a string");
        err.status = 400;
        throw err;
    }

    const s = x.trim();

    if (s.length < USERNAME_MIN_LEN || s.length > USERNAME_MAX_LEN) {
        const err = new Error(`username length must be ${USERNAME_MIN_LEN}-${USERNAME_MAX_LEN} characters`);
        err.status = 400;
        throw err;
    }

    return s;
}

// user_id: strictly 6 digits
function requireUserId6Digits(x) {
    if (typeof x !== "string") {
        const err = new Error("user_id must be a string");
        err.status = 400;
        throw err;
    }

    const s = x.trim();

    if (!/^\d{6}$/.test(s)) {
        const err = new Error("user_id must be 6 digits");
        err.status = 400;
        throw err;
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
            const err = new Error("User with the same username already exists");
            err.status = 409;
            throw err;
        }

        if (exists.username === u) {
            const err = new Error("Username already exists");
            err.status = 409;
            throw err;
        }

        if (exists.user_id === id) {
            const err = new Error("Try again");
            err.status = 409;
            throw err;
        }

        const err = new Error("User already exists");
        err.status = 409;
        throw err;
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
    }
    catch (err) {
        const msg = String(err && err.message ? err.message : err);

        if (msg.includes("users.username")) {
            const e = new Error("Username already exists");
            e.status = 409;
            throw e;
        }

        if (msg.includes("users.user_id")) {
            const e = new Error("wrong username or user_id");
            e.status = 409;
            throw e;
        }

        if (msg.includes("ux_users_username_user_id")) {
            const e = new Error("wrong username or user_id");
            e.status = 409;
            throw e;
        }

        if (msg.includes("SQLITE_CONSTRAINT")) {
            const e = new Error("User already exists");
            e.status = 409;
            throw e;
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

    if (!body || typeof body !== "object") {
        const err = new Error("Body must be JSON");
        err.status = 400;
        throw err;
    }

    if (!isISODateString(body.start_time)) {
        const err = new Error("start_time must be an ISO date string");
        err.status = 400;
        throw err;
    }

    if (!isISODateString(body.end_time)) {
        const err = new Error("end_time must be an ISO date string");
        err.status = 400;
        throw err;
    }

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