import "dotenv/config";
import express from "express";
import helmet from "helmet";
import rateLimit from "express-rate-limit";
import { migrate } from "./migrate.js";
import { registerAuthRoutes } from "./routes/auth.js";

const app = express();
const port = Number(process.env.PORT || 3000);

app.use(helmet());
app.use(express.json({ limit: "32kb" }));

const authLimiter = rateLimit({
  windowMs: 15 * 60 * 1000,
  max: 60,
  standardHeaders: true,
  legacyHeaders: false,
});

app.get("/health", (_req, res) => {
  res.json({ ok: true });
});

app.use("/auth", authLimiter);
registerAuthRoutes(app);

async function main() {
  await migrate();
  app.listen(port, () => {
    console.log(`Auth API listening on port ${port}`);
  });
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
