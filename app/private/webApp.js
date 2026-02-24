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

// Serve frontend assets (Daniel’s public folder)
app.use(express.static(path.join(__dirname, "../public")));

// JSON bodies
app.use(express.json());

// Allow GitHub Pages (and other origins) to call this backend
app.use(cors());

// Sessions
app.use(
    session({
        // use a temp string if you don't have .env: secret: "temp_key"
        secret: process.env.SESSION_SECRET || "temp_key_change_me",
        resave: false,
        saveUninitialized: false,
        cookie: { secure: false },
    })
);

// ===============================
// Account creation
// ===============================
app.post("/new_user", (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    // TODO: DB: check if username/id exists; if not create
    const n = "daniel";
    const u = "2";

    if (username === n && user_id === u) {
        req.session.user = { username: n };
        res.redirect("/user-data/rope");
    } else {
        res.status(401).json({ error: "Invalid credentials" });
    }
});

// ===============================
// Rate limits
// ===============================
const login_ip_limiter = rateLimit({
    windowMs: 10 * 60 * 1000,
    max: 40,
    standardHeaders: true,
    legacyHeaders: false,
    keyGenerator: (req) => ipKeyGenerator(req.ip),
    message: { error: "Too many attempts from this IP. Try again later." },
});

const login_user_failed_limiter = rateLimit({
    windowMs: 10 * 60 * 1000,
    max: 20,
    standardHeaders: true,
    legacyHeaders: false,
    keyGenerator: (req) => {
        const u = (req.body?.username || "").trim();
        return u ? `user:${u}` : `ip:${ipKeyGenerator(req.ip)}`;
    },
    skipSuccessfulRequests: true,
    requestWasSuccessful: (req, res) => res.statusCode < 400,
    message: { error: "Too many failed attempts for this username. Try again later." },
});

// ===============================
// Login
// ===============================
app.post("/login", login_ip_limiter, login_user_failed_limiter, (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    // TODO: DB: check does username and id correct comparing with db
    const test_name = "daniel";
    const test_id = "2";

    if (username === test_name && user_id === test_id) {
        req.session.user = { username: test_name };
        res.redirect("/user-data/rope");
    } else {
        res.status(401).json({ error: "Invalid username or user ID" });
    }
});

// ===============================
// Auth middleware
// ===============================
function authMiddleware(req, res, next) {
    if (!req.session || !req.session.user) {
        return res.status(401).json({ error: "Not authorized" });
    }
    next();
}

// ===============================
// Signed-in user route
// ===============================
app.get("/user-data/rope", authMiddleware, (req, res) => {
    res.sendFile(path.join(__dirname, "../public/data.html"));
});

// ===============================
// User info endpoint
// ===============================
app.get("/data/user", authMiddleware, (req, res) => {
    const user = req.session.user;
    res.json({ username: user.username });
});

// ===============================
// Logout
// ===============================
app.post("/logout", (req, res) => {
    if (!req.session) {
        console.log(`session doesn't exist`);
        return res.redirect("/");
    }

    req.session.destroy((err) => {
        if (err) {
            console.error("Logout error:", err);
            return res.status(500).send("Logout failed");
        }

        res.clearCookie("connect.sid");
        res.redirect("/");
        console.log(`session destroyed`);
    });
});

// ===============================
// Workouts API (no auth for now)
// ===============================

app.post("/api/workouts", async (req, res) => {
    try {
        const workout = await db.insertWorkout(req.body);
        res.status(201).json({ ok: true, workout });
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

// ===============================
// Start server after DB is ready
// ===============================
db.init()
    .then(() => {
        app.listen(port, () => {
            console.log(`server running on  ${port}`);
        });
    })
    .catch((err) => {
        console.error("DB init failed:", err);
        process.exit(1);
    });