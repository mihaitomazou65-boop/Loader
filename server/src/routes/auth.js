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

      if (!validateName(name)) {
        return res.status(400).json({ message: "Name must be 3-24 characters" });
      }
      if (!validatePassword(password)) {
        return res.status(400).json({ message: "Password must be at least 8 characters" });
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
        `INSERT INTO users (email, password_hash) VALUES ($1, $2)
         RETURNING id, email, created_at`,
        [name, password_hash]
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

      if (!validateName(name) || typeof password !== "string") {
        return res.status(401).json({ message: "Wrong name or password" });
      }

      const result = await pool.query(
        `SELECT id, email, password_hash FROM users WHERE LOWER(email) = $1`,
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
        `SELECT id, email, created_at FROM users WHERE id = $1`,
        [req.user.id]
      );
      const user = result.rows[0];
      if (!user) {
        return res.status(401).json({ message: "Unauthorized" });
      }
      return res.json({ user: { id: user.id, name: user.email } });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });
}
