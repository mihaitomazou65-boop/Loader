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
      CREATE UNIQUE INDEX IF NOT EXISTS users_name_lower_idx
      ON users (LOWER(email));
    `);
  } catch (err) {
    console.error("name index", err.message);
  }

  try {
    await pool.query(`
      ALTER TABLE users
        ADD COLUMN IF NOT EXISTS bind_hwid TEXT,
        ADD COLUMN IF NOT EXISTS bind_ip TEXT,
        ADD COLUMN IF NOT EXISTS last_seen_ip TEXT,
        ADD COLUMN IF NOT EXISTS last_seen_at TIMESTAMPTZ,
        ADD COLUMN IF NOT EXISTS sub_product TEXT,
        ADD COLUMN IF NOT EXISTS sub_expires_at TIMESTAMPTZ,
        ADD COLUMN IF NOT EXISTS sub_lifetime BOOLEAN NOT NULL DEFAULT FALSE,
        ADD COLUMN IF NOT EXISTS banned BOOLEAN NOT NULL DEFAULT FALSE
    `);
  } catch (err) {
    console.error("bind columns", err.message);
  }

  try {
    await pool.query(`
      CREATE TABLE IF NOT EXISTS license_keys (
        id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
        key_hash TEXT NOT NULL UNIQUE,
        prefix TEXT NOT NULL,
        created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        created_ip TEXT,
        redeemed_at TIMESTAMPTZ,
        redeemed_by UUID REFERENCES users(id)
      );
    `);
  } catch (err) {
    console.error("license_keys", err.message);
  }

  try {
    await pool.query(`
      ALTER TABLE license_keys
        ADD COLUMN IF NOT EXISTS product TEXT NOT NULL DEFAULT 'FiveM',
        ADD COLUMN IF NOT EXISTS duration_code TEXT NOT NULL DEFAULT '30d',
        ADD COLUMN IF NOT EXISTS duration_seconds INTEGER,
        ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ,
        ADD COLUMN IF NOT EXISTS note TEXT,
        ADD COLUMN IF NOT EXISTS cancelled_at TIMESTAMPTZ
    `);
  } catch (err) {
    console.error("license_keys cols", err.message);
  }

  try {
    await pool.query(`
      CREATE TABLE IF NOT EXISTS product_files (
        product TEXT PRIMARY KEY,
        filename TEXT NOT NULL,
        version TEXT NOT NULL,
        data BYTEA NOT NULL,
        updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
      );
    `);
  } catch (err) {
    console.error("product_files", err.message);
  }
}
