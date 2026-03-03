require("dotenv").config();

const express = require("express");
const app = express();

const path = require("path");
const rateLimit = require("express-rate-limit");
const { ipKeyGenerator } = require("express-rate-limit");
const session = require("express-session");

const cors = require("cors");
const db = require("./data_base");

const port = 3000;

// Serve frontend assets (app/public)
app.use(express.static(path.join(__dirname, "../public")));

// JSON bodies (small limit is enough for demo)
app.use(express.json({ limit: "32kb" }));

// CORS (demo)
app.use(cors());

// Sessions (demo)
app.use(
    session({
        secret: process.env.SESSION_SECRET || "temp_key_change_me",
        resave: false,
        saveUninitialized: false,
        cookie: { secure: false }, // localhost
    })
);

// ===============================
// Demo account creation
// ===============================
app.post("/new_user", (req, res) => {
    const { username, user_id } = req.body || {};
    console.log("new_user:", username, user_id);

    // Demo: single test user
    const n = "daniel";
    const u = "2";

    if (username === n && user_id === u) {
        req.session.user = { username: n };
        return res.redirect("/user-data/rope");
    }

    return res.status(401).json({ error: "Invalid credentials" });
});

// ===============================
// Rate limits (demo)
// ===============================
const login_ip_limiter = rateLimit({
    windowMs: 1 * 60 * 1000,
    limit: 40,
    standardHeaders: true,
    legacyHeaders: false,
    keyGenerator: (req) => ipKeyGenerator(req.ip),
    skipSuccessfulRequests: true,
    requestWasSuccessful: (req, res) => res.statusCode < 400,
    message: { error: "Too many attempts (IP). Try again later." },
});

const login_user_failed_limiter = rateLimit({
    windowMs: 5 * 60 * 1000,
    limit: 10,
    standardHeaders: true,
    legacyHeaders: false,
    keyGenerator: (req) => {
        const u = (req.body?.username || "").trim();
        return u ? `user:${u}` : `ip:${ipKeyGenerator(req.ip)}`;
    },
    skipSuccessfulRequests: true,
    requestWasSuccessful: (req, res) => res.statusCode < 400,
    message: { error: "Too many attempts (user). Try again later." },
});

// ===============================
// Login (demo)
// ===============================
app.post("/login", login_ip_limiter, login_user_failed_limiter, (req, res) => {
    const { username, user_id } = req.body || {};
    console.log("login:", username, user_id);

    // Demo: single test user
    const test_name = "daniel";
    const test_id = "2";

    if (username === test_name && user_id === test_id) {
        req.session.user = { username: test_name };
        return res.redirect("/user-data/rope");
    }

    return res.status(401).json({ error: "Invalid username or ID" });
});

// ===============================
// Auth middleware (demo)
// ===============================
function authMiddleware(req, res, next) {
    if (!req.session || !req.session.user) {
        return res.status(401).json({ error: "Unauthorized" });
    }
    next();
}

// ===============================
// Post-login routes
// ===============================
app.get("/user-data/rope", authMiddleware, (req, res) => {
    return res.sendFile(path.join(__dirname, "../public/data.html"));
});

app.get("/data/user", authMiddleware, (req, res) => {
    const user = req.session.user;
    return res.json({ username: user.username });
});

// ===============================
// Logout
// ===============================
app.post("/logout", (req, res) => {
    if (!req.session) {
        console.log("logout: no session");
        return res.redirect("/");
    }

    req.session.destroy((err) => {
        if (err) {
            console.error("Logout error:", err);
            return res.status(500).send("Logout failed");
        }

        res.clearCookie("connect.sid");
        res.redirect("/");
        console.log("logout: session destroyed");
    });
});

// ===============================
// Workout payload validation (demo)
// ===============================
function isISODateString(s) {
    return typeof s === "string" && s.trim() && !Number.isNaN(Date.parse(s));
}

function validateWorkoutBody(req, res, next) {
    const b = req.body;

    if (!b || typeof b !== "object" || Array.isArray(b)) {
        return res.status(400).json({ ok: false, error: "Body must be a JSON object" });
    }

    const required = ["start_time", "end_time", "duration_ms", "jump_count"];
    for (const k of required) {
        if (!(k in b)) {
            return res.status(400).json({ ok: false, error: `Missing required field: ${k}` });
        }
    }

    if (!isISODateString(b.start_time) || !isISODateString(b.end_time)) {
        return res.status(400).json({ ok: false, error: "start_time/end_time must be ISO-8601 strings" });
    }

    // Sanitize the object to avoid unexpected fields (demo)
    req.body = {
        start_time: String(b.start_time).trim(),
        end_time: String(b.end_time).trim(),
        duration_ms: b.duration_ms,
        jump_count: b.jump_count,
        avg_heart_rate_bpm: (b.avg_heart_rate_bpm === undefined) ? null : b.avg_heart_rate_bpm,
        max_heart_rate_bpm: (b.max_heart_rate_bpm === undefined) ? null : b.max_heart_rate_bpm,
        device_name: (typeof b.device_name === "string" && b.device_name.trim())
            ? b.device_name.trim().slice(0, 64)
            : "JRope-C6",
    };

    next();
}

// ===============================
// Workouts API (demo)
// ===============================
app.post("/api/workouts", validateWorkoutBody, async (req, res) => {
    try {
        const workoutRow = await db.insertWorkout(req.body);
        res.status(201).json({ ok: true, workout: workoutRow });
    } catch (err) {
        res.status(400).json({ ok: false, error: String(err.message || err) });
    }
});

app.get("/api/workouts", async (req, res) => {
    try {
        const workouts = await db.listWorkouts(req.query.limit);
        res.json({ ok: true, workouts });
    } catch (err) {
        res.status(500).json({ ok: false, error: String(err.message || err) });
    }
});

// Start server after DB init
db.init()
    .then(() => {
        app.listen(port, () => {
            console.log(`server running on ${port}`);
        });
    })
    .catch((err) => {
        console.error("DB init failed:", err);
        process.exit(1);
    });