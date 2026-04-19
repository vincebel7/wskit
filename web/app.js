require("dotenv").config();

// Redis
const redis = require('redis');
const redis_client = redis.createClient({ socket: { port: 6379 } });
const redis_subscriber = redis_client.duplicate();
redis_subscriber.connect();

redis_subscriber.on('ready', () => console.log("Connected to Redis"));
redis_subscriber.on('error', err => console.error("Error connecting to Redis", err));

// MySQL
const USE_MYSQL = process.env.USE_MYSQL?.toLowerCase() === 'true';
let pool;
if (USE_MYSQL) {
    const mysql = require('mysql2/promise');
    pool = mysql.createPool({
        host: process.env.DB_HOST,
        user: process.env.DB_USER,
        password: process.env.DB_PASS,
        database: process.env.DB_NAME,
        port: 3306,
        waitForConnections: true,
        connectionLimit: 10,
    });
    console.log("MySQL pool initialized");
}

// Web server
const http = require('http');
const express = require('express');
const app = express();

app.use(express.json());

const server = http.createServer(app);
const io = require('socket.io')(server);

console.log("Listening on 8080");

app.get('/', function (req, res) {
    console.log("Webpage request");
    res.sendFile(__dirname + "/index.html");
});

app.get('/history',    (_req, res) => res.sendFile(__dirname + "/history.html"));
app.get('/collectors', (_req, res) => res.sendFile(__dirname + "/collectors.html"));

app.use(express.static('public'));

// --- REST API ---

function mysqlRequired(_req, res, next) {
    if (!USE_MYSQL) return res.status(503).json({ error: "MySQL is not enabled" });
    next();
}

const COLLECTOR_EDITABLE_FIELDS = ['name', 'device_type', 'sensor_type', 'location'];

// GET /api/collectors
app.get('/api/collectors', mysqlRequired, async (req, res) => {
    try {
        const [rows] = await pool.query("SELECT * FROM collectors ORDER BY created_at DESC");
        res.json(rows);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

// GET /api/collectors/:id
app.get('/api/collectors/:id', mysqlRequired, async (req, res) => {
    try {
        const [rows] = await pool.query("SELECT * FROM collectors WHERE id = ?", [req.params.id]);
        if (!rows.length) return res.status(404).json({ error: "Collector not found" });
        res.json(rows[0]);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

// PATCH /api/collectors/:id  — update name, device_type, sensor_type, location
app.patch('/api/collectors/:id', mysqlRequired, async (req, res) => {
    const updates = {};
    for (const field of COLLECTOR_EDITABLE_FIELDS) {
        if (req.body[field] !== undefined) updates[field] = req.body[field];
    }
    if (!Object.keys(updates).length) {
        return res.status(400).json({ error: `No valid fields. Editable: ${COLLECTOR_EDITABLE_FIELDS.join(', ')}` });
    }
    try {
        const [check] = await pool.query("SELECT id FROM collectors WHERE id = ?", [req.params.id]);
        if (!check.length) return res.status(404).json({ error: "Collector not found" });

        const setClauses = Object.keys(updates).map(f => `\`${f}\` = ?`).join(', ');
        await pool.query(
            `UPDATE collectors SET ${setClauses} WHERE id = ?`,
            [...Object.values(updates), req.params.id]
        );
        const [rows] = await pool.query("SELECT * FROM collectors WHERE id = ?", [req.params.id]);
        res.json(rows[0]);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

// Shared readings query used by both /api/readings and /api/collectors/:id/readings
// Query params: from (ISO timestamp), to (ISO timestamp), limit (default 100, max 1000)
// /api/readings also accepts: collector_id
async function queryReadings(req, res, collectorId = null) {
    const limit = Math.min(parseInt(req.query.limit) || 100, 1000);
    const filterCollectorId = collectorId || req.query.collector_id || null;

    const conditions = [];
    const params = [];

    if (filterCollectorId) {
        conditions.push("r.collector_id = ?");
        params.push(filterCollectorId);
    }
    if (req.query.from) {
        conditions.push("r.recorded_at >= ?");
        params.push(req.query.from);
    }
    if (req.query.to) {
        conditions.push("r.recorded_at <= ?");
        params.push(req.query.to);
    }

    const where = conditions.length ? `WHERE ${conditions.join(' AND ')}` : '';
    params.push(limit);

    try {
        const [rows] = await pool.query(
            `SELECT r.*, c.name AS collector_name
             FROM sensor_readings r
             JOIN collectors c ON r.collector_id = c.id
             ${where}
             ORDER BY r.recorded_at DESC
             LIMIT ?`,
            params
        );
        res.json(rows);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
}

// GET /api/readings?collector_id=&from=&to=&limit=
app.get('/api/readings', mysqlRequired, (req, res) => queryReadings(req, res));

// GET /api/collectors/:id/readings?from=&to=&limit=
app.get('/api/collectors/:id/readings', mysqlRequired, (req, res) => queryReadings(req, res, req.params.id));

// --- End REST API ---

// On socket connection
io.on('connection', function (socket) {
    console.log("New socket connection established");

    (async () => {
        await redis_subscriber.subscribe('DHT-data', (message) => {
            socket.emit("data-from-server", message);
        });
    })();
});

server.listen(8080);
