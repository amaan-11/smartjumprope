const express = require('express');
const app = express();
const path = require('path');
const port = 3000;

const session = require('express-session');

app.use(express.static(path.join(__dirname, '../public')));
app.use(express.json());

const temp_key = "fredvccxv4535dfs";

app.use(session({
  secret: temp_key,
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

app.post('/login', (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    const n = "daniel";
    const u = "2";
    if (username === n && user_id === u) {

        req.session.user = { username: n };

        res.redirect('/user-data/rope');
    }
    else {
        res.redirect('/');
    } 

    //res.sendFile(__dirname + '/data.html');
});


app.get('/user-data/rope', (req, res) => {

    res.sendFile('data.html', { root: path.join(__dirname, '../public') });
    //res.sendFile(path.join(__dirname, '../public/data.html'));
});


function authMiddleware(req, res, next) {
  if (!req.session || !req.session.user) {
    return res.status(401).json({ error: "Not authorized" });
  }
  next();
}

app.get('/api/me', authMiddleware, (req, res) => {
  const user = req.session.user;

  res.json({
    username: user.username,
  });
});


app.listen(port, () => {
    console.log(`server running on  ${port}`);
});

