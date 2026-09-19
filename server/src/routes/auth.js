import bcrypt from "bcryptjs";
import { pool } from "../db.js";
import { authMiddleware, signToken } from "../middleware/auth.js";

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

function validateEmail(email) {
  return typeof email === "string" && EMAIL_RE.test(email.trim().toLowerCase());
}

function validatePassword(password) {
  return typeof password === "string" && password.length >= 8;
}

export function registerAuthRoutes(app) {
  app.post("/auth/signup", async (req, res) => {
    try {
      const email = String(req.body?.email || "").trim().toLowerCase();
      const password = req.body?.password;

      if (!validateEmail(email)) {
        return res.status(400).json({ message: "Invalid email" });
      }
      if (!validatePassword(password)) {
        return res.status(400).json({ message: "Password must be at least 8 characters" });
      }

      const password_hash = await bcrypt.hash(password, 12);
      const result = await pool.query(
        `INSERT INTO users (email, password_hash) VALUES ($1, $2)
         RETURNING id, email, created_at`,
        [email, password_hash]
      );

      const user = result.rows[0];
      const token = signToken(user);
      return res.status(201).json({ token, user: { id: user.id, email: user.email } });
    } catch (err) {
      if (err.code === "23505") {
        return res.status(409).json({ message: "Email already registered" });
      }
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.post("/auth/login", async (req, res) => {
    try {
      const email = String(req.body?.email || "").trim().toLowerCase();
      const password = req.body?.password;

      if (!validateEmail(email) || typeof password !== "string") {
        return res.status(401).json({ message: "Invalid email or password" });
      }

      const result = await pool.query(
        `SELECT id, email, password_hash FROM users WHERE email = $1`,
        [email]
      );
      const row = result.rows[0];
      if (!row) {
        return res.status(401).json({ message: "Invalid email or password" });
      }

      const ok = await bcrypt.compare(password, row.password_hash);
      if (!ok) {
        return res.status(401).json({ message: "Invalid email or password" });
      }

      const token = signToken(row);
      return res.json({ token, user: { id: row.id, email: row.email } });
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
      return res.json({ user });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });
}
