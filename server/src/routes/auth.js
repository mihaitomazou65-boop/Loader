import bcrypt from "bcryptjs";
import { pool } from "../db.js";
import { authMiddleware, signToken } from "../middleware/auth.js";

function readName(body) {
  const raw = body?.name ?? body?.email ?? "";
  return String(raw).trim().toLowerCase();
}

function validateName(name) {
  return typeof name === "string" && name.length >= 3 && name.length <= 24;
}

function validatePassword(password) {
  return typeof password === "string" && password.length >= 8;
}

function readHwid(body) {
  const h = String(body?.hwid || "").trim().toLowerCase();
  if (!/^[a-f0-9]{64}$/.test(h)) return "";
  return h;
}

function clientIp(req) {
  const raw = String(req.ip || "").split(",")[0].trim();
  return raw.replace(/^::ffff:/i, "");
}

function bindConflict(row, hwid, ip) {
  if (!row.bind_hwid || !row.bind_ip) return null;
  if (row.bind_hwid !== hwid) return "Account locked to another device";
  if (row.bind_ip !== ip) return "Account locked to another network";
  return null;
}

export function registerAuthRoutes(app) {
  app.use("/auth", (req, res, next) => {
    if (!process.env.DATABASE_URL) {
      return res.status(503).json({ message: "Database is not configured" });
    }
    next();
  });

  app.post("/auth/signup", async (req, res) => {
    try {
      const name = readName(req.body);
      const password = req.body?.password;
      const hwid = readHwid(req.body);
      const ip = clientIp(req);

      if (!validateName(name)) {
        return res.status(400).json({ message: "Name must be 3-24 characters" });
      }
      if (!validatePassword(password)) {
        return res.status(400).json({ message: "Password must be at least 8 characters" });
      }
      if (!hwid || !ip) {
        return res.status(400).json({ message: "Device bind required" });
      }

      const existing = await pool.query(
        `SELECT id FROM users WHERE LOWER(email) = $1 LIMIT 1`,
        [name]
      );
      if (existing.rowCount > 0) {
        return res.status(409).json({ message: "Name already taken" });
      }

      const password_hash = await bcrypt.hash(password, 12);
      const result = await pool.query(
        `INSERT INTO users (email, password_hash, bind_hwid, bind_ip)
         VALUES ($1, $2, $3, $4)
         RETURNING id, email, created_at`,
        [name, password_hash, hwid, ip]
      );

      const user = result.rows[0];
      const token = signToken({ id: user.id, email: user.email });
      return res.status(201).json({ token, user: { id: user.id, name: user.email } });
    } catch (err) {
      if (err.code === "23505") {
        return res.status(409).json({ message: "Name already taken" });
      }
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.post("/auth/login", async (req, res) => {
    try {
      const name = readName(req.body);
      const password = req.body?.password;
      const hwid = readHwid(req.body);
      const ip = clientIp(req);

      if (!validateName(name) || typeof password !== "string") {
        return res.status(401).json({ message: "Wrong name or password" });
      }
      if (!hwid || !ip) {
        return res.status(400).json({ message: "Device bind required" });
      }

      const result = await pool.query(
        `SELECT id, email, password_hash, bind_hwid, bind_ip
         FROM users WHERE LOWER(email) = $1`,
        [name]
      );
      const row = result.rows[0];
      if (!row) {
        return res.status(401).json({ message: "Wrong name or password" });
      }

      const ok = await bcrypt.compare(password, row.password_hash);
      if (!ok) {
        return res.status(401).json({ message: "Wrong name or password" });
      }

      if (!row.bind_hwid || !row.bind_ip) {
        await pool.query(
          `UPDATE users SET bind_hwid = $1, bind_ip = $2 WHERE id = $3 AND bind_hwid IS NULL`,
          [hwid, ip, row.id]
        );
      } else {
        const locked = bindConflict(row, hwid, ip);
        if (locked) {
          return res.status(403).json({ message: locked });
        }
      }

      const token = signToken({ id: row.id, email: row.email });
      return res.json({ token, user: { id: row.id, name: row.email } });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.get("/auth/me", authMiddleware, async (req, res) => {
    try {
      const result = await pool.query(
        `SELECT id, email, created_at, bind_ip FROM users WHERE id = $1`,
        [req.user.id]
      );
      const user = result.rows[0];
      if (!user) {
        return res.status(401).json({ message: "Unauthorized" });
      }
      const ip = clientIp(req);
      if (user.bind_ip && user.bind_ip !== ip) {
        return res.status(403).json({ message: "Account locked to another network" });
      }
      return res.json({ user: { id: user.id, name: user.email } });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });
}
