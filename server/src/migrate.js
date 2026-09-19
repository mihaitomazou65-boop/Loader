import { hasDatabase, pool } from "./db.js";

export async function migrate() {
  if (!hasDatabase()) {
    console.warn("skip migrate: DATABASE_URL is not set");
    return;
  }

  try {
    await pool.query(`CREATE EXTENSION IF NOT EXISTS pgcrypto`);
  } catch (err) {
    console.error("pgcrypto", err.message);
  }

  try {
    await pool.query(`
      CREATE TABLE IF NOT EXISTS users (
        id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
        email TEXT NOT NULL UNIQUE,
        password_hash TEXT NOT NULL,
        created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
      );
    `);
  } catch (err) {
    console.error("users table", err.message);
  }

  try {
    await pool.query(`
      ALTER TABLE users
        ADD COLUMN IF NOT EXISTS bind_hwid TEXT,
        ADD COLUMN IF NOT EXISTS bind_ip TEXT
    `);
  } catch (err) {
    console.error("bind columns", err.message);
  }
}
