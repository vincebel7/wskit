require("dotenv").config();

// MySQL
const mysql = require('mysql2/promise');
const pool = mysql.createPool({
    host: process.env.DB_HOST,
    user: process.env.DB_USER,
    password: process.env.DB_PASS,
    database: process.env.DB_NAME,
    port: 3306,
    waitForConnections: true,
    connectionLimit: 10,
});
console.log("MySQL pool initialized");

// Web server
const http = require('http');
const express = require('express');
const app = express();

app.use(express.json());

const server = http.createServer(app);
const io = require('socket.io')(server);

console.log("Listening on 8080");

app.get('/', (_req, res) => {
    console.log("Webpage request");
    res.sendFile(__dirname + "/index.html");
});

app.get('/history',    (_req, res) => res.sendFile(__dirname + "/history.html"));
app.get('/settings',   (_req, res) => res.sendFile(__dirname + "/settings.html"));

app.use(express.static('public'));

// --- Live feed: poll MySQL every 5s, push new rows to all connected clients ---

let lastSeenId = 0;

// Initialize lastSeenId to current MAX so we don't replay history on startup
(async () => {
    try {
        const [[row]] = await pool.query("SELECT MAX(id) AS maxId FROM sensor_readings");
        lastSeenId = row.maxId ?? 0;
        console.log(`Live feed initialized, lastSeenId=${lastSeenId}`);
    } catch (e) {
        console.error("Failed to initialize lastSeenId:", e.message);
    }
})();

setInterval(async () => {
    try {
        const [rows] = await pool.query(
            `SELECT r.id, r.collector_id, r.recorded_at, r.temperature, r.humidity, r.pressure,
                    c.name AS collector_name, c.lat, c.lng
             FROM sensor_readings r
             JOIN collectors c ON r.collector_id = c.id
             WHERE r.id > ?
             ORDER BY r.id ASC`,
            [lastSeenId]
        );
        if (rows.length) {
            rows.forEach(row => io.emit("data-from-server", JSON.stringify(row)));
            lastSeenId = rows[rows.length - 1].id;
        }
    } catch (e) {
        console.error("Poll error:", e.message);
    }
}, 5000);

io.on('connection', _socket => console.log("New socket connection established"));

// --- REST API ---

const COLLECTOR_EDITABLE_FIELDS = ['name', 'device_type', 'sensor_type', 'location', 'lat', 'lng'];

// GET /api/collectors
app.get('/api/collectors', async (_req, res) => {
    try {
        const [rows] = await pool.query(
            `SELECT c.*, MAX(r.recorded_at) AS last_seen
             FROM collectors c
             LEFT JOIN sensor_readings r ON r.collector_id = c.id
             GROUP BY c.id
             ORDER BY c.created_at DESC`
        );
        res.json(rows);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

// GET /api/collectors/:id
app.get('/api/collectors/:id', async (req, res) => {
    try {
        const [rows] = await pool.query("SELECT * FROM collectors WHERE id = ?", [req.params.id]);
        if (!rows.length) return res.status(404).json({ error: "Collector not found" });
        res.json(rows[0]);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

// PATCH /api/collectors/:id  — update name, device_type, sensor_type, location
app.patch('/api/collectors/:id', async (req, res) => {
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
app.get('/api/readings', (req, res) => queryReadings(req, res));

// GET /api/collectors/:id/readings?from=&to=&limit=
app.get('/api/collectors/:id/readings', (req, res) => queryReadings(req, res, req.params.id));

// --- End REST API ---

server.listen(8080);
