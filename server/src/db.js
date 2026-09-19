import pg from "pg";

const { Pool } = pg;

function createPool() {
  const connectionString = process.env.DATABASE_URL;
  if (!connectionString) {
    throw new Error("DATABASE_URL is required");
  }

  const ssl =
    process.env.NODE_ENV === "production"
      ? { rejectUnauthorized: false }
      : undefined;

  return new Pool({ connectionString, ssl });
}

export const pool = createPool();
