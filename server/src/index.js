import "dotenv/config";
import { randomBytes } from "crypto";
import express from "express";
import helmet from "helmet";
import rateLimit from "express-rate-limit";
import { hasDatabase } from "./db.js";
import { migrate } from "./migrate.js";
import { registerAuthRoutes } from "./routes/auth.js";

if (!process.env.JWT_SECRET || process.env.JWT_SECRET.length < 16) {
  process.env.JWT_SECRET = randomBytes(32).toString("hex");
  console.warn("JWT_SECRET missing; using a temporary secret for this boot");
}

const app = express();
const port = Number(process.env.PORT || 3000);

app.set("trust proxy", 1);
app.use(helmet());
app.use(express.json({ limit: "32kb" }));

app.get("/health", (_req, res) => {
  res.json({ ok: true, db: hasDatabase() });
});

const authLimiter = rateLimit({
  windowMs: 15 * 60 * 1000,
  max: 80,
  standardHeaders: true,
  legacyHeaders: false,
});
app.use("/auth", authLimiter);
registerAuthRoutes(app);

app.use((err, _req, res, _next) => {
  console.error(err);
  res.status(500).json({ message: "Server error" });
});

async function main() {
  try {
    await migrate();
  } catch (err) {
    console.error("migrate failed", err);
  }
  app.listen(port, "0.0.0.0", () => {
    console.log(`Auth API listening on port ${port}`);
  });
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
