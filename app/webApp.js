const express = require('express');
const app = express();
const port = 3000;

app.use(express.static(__dirname));
app.use(express.json());

app.get('/', (req, res) => {
    res.sendFile(__dirname + '/webApp.html');
});


app.post('/login', (req, res) => {
    const { username, user_id } = req.body;
    console.log(username, user_id);

    res.send('<h1>Welcome!</h1>');
});


app.listen(port, () => {
    console.log(`server running on  ${port}`);
});

