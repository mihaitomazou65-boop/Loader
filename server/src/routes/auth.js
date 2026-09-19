import bcrypt from "bcryptjs";
import { pool } from "../db.js";
import { authMiddleware, signToken } from "../middleware/auth.js";
import { clientIp, isPrivateIp } from "../util/ip.js";
import { defaultProduct, durationByCode } from "../util/duration.js";
import { hashKey } from "./admin.js";

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

function bindConflict(row, hwid, ip) {
  if (!row.bind_hwid || !row.bind_ip) return null;
  if (row.bind_hwid !== hwid) return "Account locked to another device";
  if (row.bind_ip && !isPrivateIp(row.bind_ip) && row.bind_ip !== ip) {
    return "Account locked to another network";
  }
  return null;
}

function publicUser(row) {
  const lifetime = Boolean(row.sub_lifetime);
  const exp = row.sub_expires_at ? new Date(row.sub_expires_at) : null;
  const active = lifetime || (exp && exp.getTime() > Date.now());
  return {
    id: row.id,
    name: row.email,
    product: active ? (row.sub_product || "") : "",
    lifetime,
    expires: row.sub_expires_at || null,
    file_name: "",
    file_version: "",
    thumb_version: "",
    thumb_fx: 0.5,
    thumb_fy: 0.5,
  };
}

async function withProductFile(row) {
  const u = publicUser(row);
  if (!u.product) return u;
  try {
    const f = await pool.query(
      `SELECT filename, version FROM product_files WHERE product = $1`,
      [u.product]
    );
    if (f.rows[0]) {
      u.file_name = f.rows[0].filename || "";
      u.file_version = String(f.rows[0].version || "");
    }
  } catch (_) {}
  try {
    const t = await pool.query(
      `SELECT version, focus_x, focus_y FROM product_thumbs WHERE product = $1`,
      [u.product]
    );
    if (t.rows[0]) {
      u.thumb_version = String(t.rows[0].version || "");
      const fx = Number(t.rows[0].focus_x);
      const fy = Number(t.rows[0].focus_y);
      u.thumb_fx = Number.isFinite(fx) ? Math.min(1, Math.max(0, fx)) : 0.5;
      u.thumb_fy = Number.isFinite(fy) ? Math.min(1, Math.max(0, fy)) : 0.5;
    }
  } catch (_) {}
  return u;
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
        `INSERT INTO users (email, password_hash, bind_hwid, bind_ip, last_seen_ip, last_seen_at)
         VALUES ($1, $2, $3, $4, $4, NOW())
         RETURNING id, email, created_at`,
        [name, password_hash, hwid, ip]
      );

      const user = result.rows[0];
      const token = signToken({ id: user.id, email: user.email });
      return res.status(201).json({ token, user: await withProductFile({ ...user, sub_product: null, sub_expires_at: null, sub_lifetime: false }) });
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
        `SELECT id, email, password_hash, bind_hwid, bind_ip, banned,
                sub_product, sub_expires_at, sub_lifetime
         FROM users WHERE LOWER(email) = $1`,
        [name]
      );
      const row = result.rows[0];
      if (!row) {
        return res.status(401).json({ message: "Wrong name or password" });
      }
      if (row.banned) {
        return res.status(403).json({ message: "Account banned" });
      }

      const ok = await bcrypt.compare(password, row.password_hash);
      if (!ok) {
        return res.status(401).json({ message: "Wrong name or password" });
      }

      if (!row.bind_hwid || !row.bind_ip || isPrivateIp(row.bind_ip)) {
        await pool.query(
          `UPDATE users SET bind_hwid = COALESCE(bind_hwid, $1), bind_ip = $2, last_seen_ip = $2, last_seen_at = NOW()
           WHERE id = $3`,
          [hwid, ip, row.id]
        );
      } else {
        const locked = bindConflict(row, hwid, ip);
        if (locked) {
          return res.status(403).json({ message: locked });
        }
        await pool.query(
          `UPDATE users SET last_seen_ip = $1, last_seen_at = NOW() WHERE id = $2`,
          [ip, row.id]
        );
      }

      const token = signToken({ id: row.id, email: row.email });
      return res.json({ token, user: await withProductFile(row) });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.get("/auth/me", authMiddleware, async (req, res) => {
    try {
      const result = await pool.query(
        `SELECT id, email, created_at, bind_ip, banned, sub_product, sub_expires_at, sub_lifetime FROM users WHERE id = $1`,
        [req.user.id]
      );
      const user = result.rows[0];
      if (!user) {
        return res.status(401).json({ message: "Unauthorized" });
      }
      if (user.banned) {
        return res.status(403).json({ message: "Account banned" });
      }
      const ip = clientIp(req);
      if (user.bind_ip && !isPrivateIp(user.bind_ip) && user.bind_ip !== ip) {
        return res.status(403).json({ message: "Account locked to another network" });
      }
      return res.json({ user: await withProductFile(user) });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.post("/auth/redeem", authMiddleware, async (req, res) => {
    let client;
    try {
      client = await pool.connect();
      const raw = String(req.body?.key || "").trim().toUpperCase().replace(/[\s\u00A0\u2010-\u2015\u2212]/g, "").replace(/[^A-Z0-9-]/g, "");
      if (raw.length < 10 || raw.length > 80) {
        return res.status(400).json({ message: "Invalid key" });
      }
      const ip = clientIp(req);
      const userRow = await client.query(
        `SELECT id, bind_ip, banned FROM users WHERE id = $1`,
        [req.user.id]
      );
      const user = userRow.rows[0];
      if (!user) return res.status(401).json({ message: "Unauthorized" });
      if (user.banned) return res.status(403).json({ message: "Account banned" });
      if (user.bind_ip && !isPrivateIp(user.bind_ip) && user.bind_ip !== ip) {
        return res.status(403).json({ message: "Account locked to another network" });
      }

      const digest = hashKey(raw);
      await client.query("BEGIN");
      const found = await client.query(
        `SELECT * FROM license_keys WHERE key_hash = $1 LIMIT 1 FOR UPDATE`,
        [digest]
      );
      const key = found.rows[0];
      if (!key) {
        await client.query("ROLLBACK");
        return res.status(404).json({ message: "Invalid key" });
      }
      if (key.cancelled_at) {
        await client.query("ROLLBACK");
        return res.status(409).json({ message: "This key has been cancelled" });
      }
      if (key.redeemed_at) {
        await client.query("ROLLBACK");
        return res.status(409).json({ message: "This key has already been used" });
      }

      const dur = durationByCode(key.duration_code);
      const lifetime = !dur.seconds;
      const expiresSql = lifetime ? null : new Date(Date.now() + dur.seconds * 1000);

      const upd = await client.query(
        `UPDATE license_keys SET redeemed_at = NOW(), redeemed_by = $1, expires_at = $3
         WHERE id = $2 AND redeemed_at IS NULL
         RETURNING id`,
        [req.user.id, key.id, expiresSql]
      );
      if (!upd.rowCount) {
        await client.query("ROLLBACK");
        return res.status(409).json({ message: "This key has already been used" });
      }

      await client.query(
        `UPDATE users SET sub_product = $1, sub_expires_at = $2, sub_lifetime = $3 WHERE id = $4`,
        [key.product || defaultProduct(), expiresSql, lifetime, req.user.id]
      );
      const fresh = await client.query(
        `SELECT id, email, sub_product, sub_expires_at, sub_lifetime FROM users WHERE id = $1`,
        [req.user.id]
      );
      await client.query("COMMIT");
      const prod = key.product || defaultProduct();
      return res.json({
        ok: true,
        message: lifetime ? `${prod} lifetime redeemed` : `${prod} key redeemed`,
        user: await withProductFile(fresh.rows[0]),
      });
    } catch (err) {
      try { if (client) await client.query("ROLLBACK"); } catch (_) {}
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    } finally {
      if (client) client.release();
    }
  });

  app.get("/auth/product-file", authMiddleware, async (req, res) => {
    try {
      const result = await pool.query(
        `SELECT id, banned, sub_product, sub_expires_at, sub_lifetime FROM users WHERE id = $1`,
        [req.user.id]
      );
      const user = result.rows[0];
      if (!user) return res.status(401).json({ message: "Unauthorized" });
      if (user.banned) return res.status(403).json({ message: "Account banned" });
      const u = publicUser(user);
      if (!u.product) return res.status(403).json({ message: "No product" });
      const file = await pool.query(
        `SELECT filename, version, data FROM product_files WHERE product = $1`,
        [u.product]
      );
      if (!file.rowCount) return res.status(404).json({ message: "No product file" });
      const row = file.rows[0];
      const name = String(row.filename || "product.exe").replace(/[^\w.\-]+/g, "_");
      res.set("Content-Type", "application/octet-stream");
      res.set("X-Product-File", name);
      res.set("X-Product-Version", String(row.version || ""));
      res.set("Content-Disposition", `attachment; filename="${name}"`);
      return res.send(row.data);
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.get("/auth/product-thumb", authMiddleware, async (req, res) => {
    try {
      const result = await pool.query(
        `SELECT id, banned, sub_product, sub_expires_at, sub_lifetime FROM users WHERE id = $1`,
        [req.user.id]
      );
      const user = result.rows[0];
      if (!user) return res.status(401).json({ message: "Unauthorized" });
      if (user.banned) return res.status(403).json({ message: "Account banned" });
      const u = publicUser(user);
      if (!u.product) return res.status(403).json({ message: "No product" });
      const file = await pool.query(
        `SELECT filename, version, mime, data FROM product_thumbs WHERE product = $1`,
        [u.product]
      );
      if (!file.rowCount) return res.status(404).json({ message: "No thumbnail" });
      const row = file.rows[0];
      res.set("Content-Type", row.mime || "image/jpeg");
      res.set("X-Thumb-Version", String(row.version || ""));
      res.set("Cache-Control", "no-store");
      return res.send(row.data);
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });
}
