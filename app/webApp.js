const express = require('express');
const app = express();
const path = require('path');
const port = 3000;

app.use(express.static(__dirname));
app.use(express.json());

app.get('/', (req, res) => {
    res.sendFile(__dirname + '/webApp.html');
});


app.post('/login', (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    const n = "daniel";
    const u = "2";
    if (username === n && user_id === u) {
        res.redirect('/data/' + encodeURIComponent(username));
    }
    else {
        res.redirect('/');
    }

    //res.sendFile(__dirname + '/data.html');
});

app.get('/data/:username', (req, res) => {
    const username = req.params.username;
    console.log(username);

    res.sendFile(path.join(__dirname, 'data.html'));
});


app.listen(port, () => {
    console.log(`server running on  ${port}`);
});

