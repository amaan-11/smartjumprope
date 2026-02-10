const express = require("express");
const app = express();
const path = require("path");
const cors = require("cors");
const sqlite3 = require("sqlite3").verbose();

const port = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());

// Serve root-level frontend files (index.html, data.html, ble.js, data.css, etc.)
const REPO_ROOT = path.join(__dirname, "..", "..");
app.use(express.static(REPO_ROOT));

// Also keep serving app/public if you still use it anywhere
app.use(express.static(path.join(__dirname, "../public")));

// ---- SQLite DB ----
const DB_PATH = path.join(__dirname, "database.db");
const db = new sqlite3.Database(DB_PATH, (err) => {
    if (err) {
        console.error("Failed to open SQLite DB:", err);
        process.exit(1);
    }
    console.log("SQLite DB opened:", DB_PATH);
});

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

async function initDb() {
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

// Validation helpers
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

// ---- Existing login routes (kept exactly) ----
app.post("/login", (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    const n = "daniel";
    const u = "2";
    if (username === n && String(user_id) === u) {
        res.redirect("/user-data/rope");
    } else {
        res.redirect("/");
    }
});

app.get("/user-data/rope", (req, res) => {
    // Prefer root-level data.html if it exists, otherwise fall back to public/data.html
    res.sendFile(path.join(REPO_ROOT, "data.html"), (err) => {
        if (err) {
            res.sendFile(path.join(__dirname, "../public/data.html"));
        }
    });
});

// ---- API: create workout ----
app.post("/api/workouts", async (req, res) => {
    try {
        const body = req.body || {};

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

        const inserted = await all("SELECT * FROM workouts WHERE id = ?", [result.lastID]);
        res.status(201).json({ ok: true, workout: inserted[0] });
    } catch (err) {
        res.status(400).json({ ok: false, error: String(err.message || err) });
    }
});

// ---- API: list recent workouts ----
app.get("/api/workouts", async (req, res) => {
    try {
        const limit = Math.min(Math.max(Number(req.query.limit || 20), 1), 100);
        const rows = await all(
            "SELECT * FROM workouts ORDER BY datetime(start_time) DESC LIMIT ?",
            [limit]
        );
        res.json({ ok: true, workouts: rows });
    } catch (err) {
        res.status(500).json({ ok: false, error: String(err.message || err) });
    }
});

initDb()
    .then(() => {
        app.listen(port, () => {
            console.log(`server running on ${port}`);
        });
    })
    .catch((err) => {
        console.error("DB init failed:", err);
        process.exit(1);
    });