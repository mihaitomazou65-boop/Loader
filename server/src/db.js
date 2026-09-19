import pg from "pg";

const { Pool } = pg;

let pool = null;

function createPool() {
  const connectionString = process.env.DATABASE_URL;
  if (!connectionString) {
    return null;
  }

  const ssl =
    process.env.NODE_ENV === "production"
      ? { rejectUnauthorized: false }
      : undefined;

  return new Pool({ connectionString, ssl });
}

export function getPool() {
  if (!process.env.DATABASE_URL) {
    const err = new Error("Database is not configured");
    err.status = 503;
    throw err;
  }
  if (!pool) {
    pool = createPool();
  }
  return pool;
}

export const pool = {
  query: (...args) => getPool().query(...args),
};

export function hasDatabase() {
  return Boolean(process.env.DATABASE_URL);
}
