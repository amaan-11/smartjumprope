
require('dotenv').config();

const express = require('express');
const app = express();

//app.set('trust proxy', 1);

const path = require('path');
const rateLimit = require("express-rate-limit");
const { ipKeyGenerator } = require("express-rate-limit");
const session = require('express-session');

const port = 3000;

app.use(express.static(path.join(__dirname, '../public')));
app.use(express.json());

//const temp_key = "fredvccxv4535dfs";

app.use(session({
  //use for secret temp_key if you dont have .env file
  secret: process.env.SESSION_SECRET,
  resave: false,
  saveUninitialized: false,
  cookie: { secure: false }
}));

//If .html not index!
/*
app.get('/', (req, res) => {
   // res.sendFile(path.join(__dirname, '../public/webApp.html'));
});
*/


//account creation
app.post('/new_user', (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    //db: compare from database is there already particular username or id
    // if not add new user in db and save user db address/id to req.session  
    const n = "daniel";
    const u = "2";
    if (username === n && user_id === u) {

        req.session.user = { username: n };

        res.redirect('/user-data/rope');
    }
    else {
        res.status(401).json({ error: "Invalid credentials" });
    } 

    //res.sendFile(__dirname + '/data.html');
});

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


//login part
app.post('/login', login_ip_limiter, login_user_failed_limiter, (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    //db: check does username and id correct comparing with db
    //if yes save user db address/id to req.session 
    const test_name = "daniel";
    const test_id = "2";
    if (username === test_name && user_id === test_id) {

        req.session.user = { username: test_name };

        res.redirect('/user-data/rope');
    }
    else {
        res.status(401).json({ error: "Invalid username or user ID" });
    } 

    //res.sendFile(__dirname + '/data.html');
});

//check is user still in session
function authMiddleware(req, res, next) {
  if (!req.session || !req.session.user) {
    return res.status(401).json({ error: "Not authorized" });
  }
  next();
}

//signed user part
app.get('/user-data/rope', authMiddleware, (req, res) => {

    //res.sendFile('data.html', { root: path.join(__dirname, '../public') });
    res.sendFile(path.join(__dirname, '../public/data.html'));
});



//db:if user in session send data from db (username, last jumps history etc.)
app.get('/data/user', authMiddleware, (req, res) => {
  const user = req.session.user;

  res.json({
    username: user.username,
  });
});


//logout part
app.post('/logout', (req, res) => {

  if (!req.session) {
    console.log(`session doesn't exist`);
    return res.redirect('/');
  }

  req.session.destroy(err => {
    if (err) {
      console.error('Logout error:', err);
      return res.status(500).send('Logout failed');
    }

    res.clearCookie('connect.sid');
    res.redirect('/');
    console.log(`session destroyed`);              
  });
});



app.listen(port, () => {
    console.log(`server running on  ${port}`);
});

