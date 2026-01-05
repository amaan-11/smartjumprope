const express = require('express');
const app = express();
const path = require('path');
const port = 3000;

app.use(express.static(path.join(__dirname, '../public')));
app.use(express.json());


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
        res.redirect('/user-data/rope');
    }
    else {
        res.redirect('/');
    } 

    //res.sendFile(__dirname + '/data.html');
});

app.get('/user-data/rope', (req, res) => {

    res.sendFile(path.join(__dirname, '../public/data.html'));
});


app.listen(port, () => {
    console.log(`server running on  ${port}`);
});

