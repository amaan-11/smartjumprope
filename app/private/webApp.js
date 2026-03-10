require("dotenv").config();

const express = require("express");
const app = express();

const path = require("path");
const rateLimit = require("express-rate-limit");
const { ipKeyGenerator } = require("express-rate-limit");
const session = require("express-session");

//uncomment if needed
//const cors = require("cors");

const db = require("./data_base");

const port = 3000;

// Serve frontend assets (Daniel’s public folder)
app.use(express.static(path.join(__dirname, "../public")));
app.use(express.json());

// Allow GitHub Pages (and other origins) to call this backend
//app.use(cors());

// Sessions
app.use(
    session({
        // use a temp string if you don't have .env: secret: "temp_key"
        secret: process.env.SESSION_SECRET || "temp_key_change_me",
        resave: false,
        saveUninitialized: false,
        cookie: {
            //secure: true if https!
            secure: false,
            //cookie secure
            //httpOnly: true,

            //CSRF block
            //sameSite: 'lax'
            },
    })
);


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
        const ip = ipKeyGenerator(req.ip);
        const u = String(req.body?.username || "").trim()

        return u
            ? `loginfail:user:${u}:ip:${ip}` : `loginfail:ip:${ip}`;
    },

    skipSuccessfulRequests: true,
    requestWasSuccessful: (req, res) => res.statusCode < 400,

    handler: (req, res) => {
        return res.status(429).json({error: "Too many failed attempts. Try again later."});
    },
});

// ===============================
// Account creation
// ===============================
app.post("/new_user", login_ip_limiter, async (req, res) => {
    try {
        const { username, user_id } = req.body;

        const user = await db.createUser({ username, user_id });

        await new Promise((resolve, reject) => {
            req.session.regenerate((err) => {
                if (err) {
                    return reject(err);
                }
                resolve();
            });
        });

        req.session.user = {
            id: user.id,
            username: user.username
        };

        await new Promise((resolve, reject) => {
            req.session.save((err) => {
                if (err) {
                    return reject(err);
                }
                resolve();
            });
        });

        return res.redirect("/user-data/rope");
    } 
    catch (err) {
        console.error("New user error:", err);

        let status = 500;
        let message = "Request error";

        if (err) {
            if (typeof err.status === "number") status = err.status;

            if (err.message) message = err.message;
        }

        if (status === 500) return res.status(500).json({error: "Server error"});

        return res.status(status).json({error: message
        });
    }
});

// ===============================
// Login
// ===============================
app.post("/login", login_ip_limiter, login_user_failed_limiter, async (req, res) => {
    try {
        const { username, user_id } = req.body;

        const user = await db.findUserByCredentials(username, user_id);
        if (!user) {
            return res.status(401).json({ error: "Invalid username or user ID" });
        }

        await new Promise((resolve, reject) => {
            req.session.regenerate((err) => {
                if (err) {
                    return reject(err);
                }
                resolve();
            });
        });

        req.session.user = {
            id: user.id,
            username: user.username
        };

        await new Promise((resolve, reject) => {
            req.session.save((err) => {
                if (err) {
                    return reject(err);
                }
                resolve();
            });
        });

        return res.redirect("/user-data/rope");
    } 
    catch (err) {
        console.error("Login error:", err);

        let status = 500;
        let message = "Request error";

        if (err) {
            if (typeof err.status === "number") status = err.status;

            if (err.message) message = err.message;
        }

        if (status === 500) return res.status(500).json({error: "Server error"});      

        return res.status(status).json({error: message});
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

// ===============================
// Workouts API (AUTH + PER USER)
// ===============================

app.post("/api/workouts", authMiddleware, async (req, res) => {
    try {
        const userId = req.session.user.id;
        const workout = await db.insertWorkoutForUser(userId, req.body);
        res.status(201).json({ ok: true, workout });
    } 
    catch (err) {
        console.error("Insert workout error:", err);

        let status = 500;
        let message = "Request error";

        if (err) {
            if (typeof err.status === "number") status = err.status;

            if (err.message) message = err.message;
        }

        if (status === 500) {
            return res.status(500).json({ok: false, error: "Server error"});
        }

        return res.status(status).json({ok: false, error: message});
    }
});

app.get("/api/workouts", authMiddleware, async (req, res) => {
    try {
        const userId = req.session.user.id;
        const workouts = await db.listWorkoutsForUser(userId, req.query.limit);
        res.json({ ok: true, workouts });
    } 
    catch (err) {
        console.error("List workouts error:", err);

        let status = 500;
        let message = "Request error";

        if (err) {
            if (typeof err.status === "number") status = err.status;

            if (err.message) message = err.message;
        }

        if (status === 500) {
            return res.status(500).json({ok: false, error: "Server error"});
        }

        return res.status(status).json({ok: false, error: message});
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