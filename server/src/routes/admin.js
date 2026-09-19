import { createHash, randomBytes } from "crypto";
import { pool } from "../db.js";
import { adminIps, clientIp, isAdminIp } from "../util/ip.js";

function pepper() {
  return process.env.JWT_SECRET || "loader";
}

function hashKey(raw) {
  const key = String(raw).trim().toUpperCase().replace(/\s+/g, "");
  return createHash("sha256").update(`${pepper()}:${key}`).digest("hex");
}

function makeKey() {
  const hex = randomBytes(8).toString("hex").toUpperCase();
  return `LOAD-${hex.slice(0, 4)}-${hex.slice(4, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}`;
}

function deny(res) {
  res.status(404).type("text/plain").send("Not found");
}

function requireAdmin(req, res, next) {
  if (!isAdminIp(req)) return deny(res);
  next();
}

const PAGE = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<meta name="robots" content="noindex,nofollow"/>
<title>Loader Admin</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; font:14px/1.45 system-ui,sans-serif; background:#080808; color:#e6e6e6; }
  main { max-width:980px; margin:0 auto; padding:28px 20px 64px; }
  h1 { font-size:18px; font-weight:600; margin:0 0 6px; }
  .sub { color:#8a8a8a; margin-bottom:22px; }
  .row { display:flex; gap:10px; flex-wrap:wrap; margin-bottom:18px; }
  button, .btn { background:#1c1c1c; color:#eee; border:1px solid #2e2e2e; border-radius:8px; padding:8px 14px; cursor:pointer; }
  button:hover { background:#262626; }
  table { width:100%; border-collapse:collapse; margin-top:10px; }
  th, td { text-align:left; padding:8px 10px; border-bottom:1px solid #1c1c1c; font-size:13px; }
  th { color:#9a9a9a; font-weight:500; }
  .ok { color:#9fd59f; }
  .bad { color:#e07070; }
  .card { background:#0e0e0e; border:1px solid #1d1d1d; border-radius:12px; padding:16px; margin-bottom:16px; }
  input { background:#111; color:#eee; border:1px solid #2a2a2a; border-radius:8px; padding:8px 10px; width:72px; }
  code { background:#161616; padding:2px 6px; border-radius:6px; }
  .keys { display:flex; flex-direction:column; gap:8px; }
  .keyline { font-family:ui-monospace,monospace; background:#121212; padding:10px 12px; border-radius:8px; }
</style>
</head>
<body>
<main>
  <h1>Loader admin</h1>
  <p class="sub">Private. Bound users, IPs, and license keys. This page is IP-locked.</p>
  <div class="card">
    <div class="row">
      <button id="gen">Generate key</button>
      <label>Count <input id="count" type="number" min="1" max="20" value="1"/></label>
      <button id="reload">Refresh</button>
    </div>
    <div id="fresh" class="keys"></div>
  </div>
  <div class="card">
    <h1>Users</h1>
    <table><thead><tr><th>Name</th><th>IP</th><th>HWID</th><th>Created</th></tr></thead><tbody id="users"></tbody></table>
  </div>
  <div class="card">
    <h1>Keys</h1>
    <table><thead><tr><th>Prefix</th><th>Status</th><th>Redeemed by</th><th>Created</th></tr></thead><tbody id="keys"></tbody></table>
  </div>
</main>
<script>
async function api(path, opt) {
  const r = await fetch(path, Object.assign({ headers: { "content-type": "application/json" } }, opt || {}));
  if (!r.ok) throw new Error("request failed");
  return r.json();
}
function esc(s) { return String(s||"").replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])); }
async function load() {
  const data = await api("/admin/api/state");
  document.getElementById("users").innerHTML = (data.users||[]).map(u =>
    "<tr><td>"+esc(u.name)+"</td><td><code>"+esc(u.ip)+"</code></td><td><code>"+esc(u.hwid)+"</code></td><td>"+esc(u.created)+"</td></tr>"
  ).join("") || "<tr><td colspan=4>No users</td></tr>";
  document.getElementById("keys").innerHTML = (data.keys||[]).map(k =>
    "<tr><td><code>"+esc(k.prefix)+"</code></td><td class='"+(k.redeemed?"ok":"")+"'>"+(k.redeemed?"redeemed":"unused")+"</td><td>"+esc(k.redeemed_by||"")+"</td><td>"+esc(k.created)+"</td></tr>"
  ).join("") || "<tr><td colspan=4>No keys</td></tr>";
}
document.getElementById("reload").onclick = () => load().catch(() => {});
document.getElementById("gen").onclick = async () => {
  const count = Number(document.getElementById("count").value || 1);
  const data = await api("/admin/api/keys", { method: "POST", body: JSON.stringify({ count }) });
  document.getElementById("fresh").innerHTML = (data.keys||[]).map(k =>
    "<div class='keyline'>"+esc(k)+"</div>"
  ).join("");
  await load();
};
load().catch((e) => { document.body.innerHTML = "<main>Failed to load</main>"; });
</script>
</body>
</html>`;

export function registerAdminRoutes(app) {
  app.get("/admin", (req, res) => {
    if (!adminIps().length) return deny(res);
    if (!isAdminIp(req)) return deny(res);
    res.set("Cache-Control", "no-store");
    res.set("X-Robots-Tag", "noindex, nofollow");
    res.type("html").send(PAGE);
  });

  app.get("/admin/api/state", requireAdmin, async (_req, res) => {
    const users = await pool.query(
      `SELECT email, bind_ip, bind_hwid, created_at FROM users ORDER BY created_at DESC LIMIT 200`
    );
    const keys = await pool.query(
      `SELECT k.prefix, k.redeemed_at, k.created_at, u.email AS redeemed_by
       FROM license_keys k
       LEFT JOIN users u ON u.id = k.redeemed_by
       ORDER BY k.created_at DESC LIMIT 200`
    );
    res.json({
      users: users.rows.map((r) => ({
        name: r.email,
        ip: r.bind_ip || "",
        hwid: r.bind_hwid ? String(r.bind_hwid).slice(0, 12) + "…" : "",
        created: r.created_at,
      })),
      keys: keys.rows.map((r) => ({
        prefix: r.prefix,
        redeemed: Boolean(r.redeemed_at),
        redeemed_by: r.redeemed_by || "",
        created: r.created_at,
      })),
    });
  });

  app.post("/admin/api/keys", requireAdmin, async (req, res) => {
    let count = Number(req.body?.count || 1);
    if (!Number.isFinite(count) || count < 1) count = 1;
    if (count > 20) count = 20;
    const made = [];
    for (let i = 0; i < count; i++) {
      const key = makeKey();
      const prefix = key.slice(0, 9);
      await pool.query(
        `INSERT INTO license_keys (key_hash, prefix, created_ip) VALUES ($1, $2, $3)`,
        [hashKey(key), prefix, clientIp(req)]
      );
      made.push(key);
    }
    res.json({ keys: made });
  });
}

export { hashKey };
