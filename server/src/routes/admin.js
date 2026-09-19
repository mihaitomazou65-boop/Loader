import { createHash, randomBytes } from "crypto";
import multer from "multer";
import { pool } from "../db.js";
import { DURATIONS, PRODUCTS, durationByCode } from "../util/duration.js";
import { adminSecretOk, clientIp, isAdminRequest, setAdminCookie } from "../util/ip.js";

function pepper() {
  return process.env.JWT_SECRET || "loader";
}

function hashKey(raw) {
  const key = String(raw || "")
    .toUpperCase()
    .replace(/[\s\u00A0\u2010-\u2015\u2212]/g, "")
    .replace(/[^A-Z0-9-]/g, "");
  return createHash("sha256").update(`${pepper()}:${key}`).digest("hex");
}

function makeKey(product) {
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  const n = 16;
  const bytes = randomBytes(n);
  let body = "";
  for (let i = 0; i < n; i++) body += alphabet[bytes[i] % alphabet.length];
  const groups = [body.slice(0, 4), body.slice(4, 8), body.slice(8, 12), body.slice(12, 16)];
  const tag = String(product || "FIVEM").replace(/[^A-Z0-9]/gi, "").slice(0, 6).toUpperCase() || "FIVEM";
  return `${tag}-${groups.join("-")}`;
}

function deny(req, res) {
  console.warn("admin deny ip=", clientIp(req), "xff=", req.headers["x-forwarded-for"] || "");
  res.status(404).type("text/plain").send("Not found");
}

function requireAdmin(req, res, next) {
  if (!isAdminRequest(req)) return deny(req, res);
  next();
}

const upload = multer({
  storage: multer.memoryStorage(),
  limits: { fileSize: 2 * 1024 * 1024 * 1024 },
});

function uuid(v) {
  const id = String(v || "");
  return /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(id) ? id : "";
}

async function clearSub(userId) {
  if (!userId) return;
  await pool.query(
    `UPDATE users SET sub_product = NULL, sub_expires_at = NULL, sub_lifetime = FALSE WHERE id = $1`,
    [userId]
  );
}

const PAGE = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<meta name="robots" content="noindex,nofollow"/>
<title>Seller</title>
<style>
  :root { color-scheme: dark; --bg:#07080c; --panel:#0e1118; --line:#1c2230; --muted:#8b93a7; --text:#e8ecf4; --accent:#6c5ce7; --ok:#3dd68c; --warn:#f5c542; }
  * { box-sizing:border-box; }
  body { margin:0; font:13px/1.45 Inter,system-ui,sans-serif; background:var(--bg); color:var(--text); display:flex; min-height:100vh; }
  aside { width:220px; background:#0a0c12; border-right:1px solid var(--line); padding:18px 12px; }
  aside h2 { margin:0 12px 18px; font-size:15px; letter-spacing:.04em; }
  .nav { display:flex; flex-direction:column; gap:4px; }
  .nav button { text-align:left; background:transparent; border:0; color:var(--muted); padding:10px 12px; border-radius:8px; cursor:pointer; }
  .nav button.on, .nav button:hover { background:#151a26; color:var(--text); }
  main { flex:1; padding:22px 26px 48px; }
  h1 { font-size:20px; margin:0 0 4px; }
  .sub { color:var(--muted); margin-bottom:18px; }
  .stats { display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:12px; margin-bottom:18px; }
  .stat { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:14px 16px; }
  .stat b { display:block; font-size:22px; margin-top:6px; }
  .stat span { color:var(--muted); }
  .card { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:16px; margin-bottom:16px; }
  .row { display:flex; gap:10px; flex-wrap:wrap; align-items:end; }
  label { display:flex; flex-direction:column; gap:6px; color:var(--muted); }
  input, select { background:#0b0e14; color:var(--text); border:1px solid var(--line); border-radius:8px; padding:8px 10px; min-width:110px; }
  button.act { background:var(--accent); color:#fff; border:0; border-radius:8px; padding:9px 14px; cursor:pointer; font-weight:600; }
  button.ghost { background:#151a26; color:var(--text); border:1px solid var(--line); border-radius:8px; padding:9px 14px; cursor:pointer; }
  button.tiny { padding:5px 8px; font-size:11px; margin-right:4px; }
  button.danger { background:#3a1518; color:#ffb4b4; border:1px solid #5a2428; border-radius:8px; cursor:pointer; }
  .bad { color:#ff6b6b; }
  .acts { white-space:nowrap; }
  .keycell { display:flex; align-items:center; gap:8px; }
  .keycell code { flex:1; }
  table { width:100%; border-collapse:collapse; margin-top:8px; }
  th, td { text-align:left; padding:9px 8px; border-bottom:1px solid var(--line); font-size:12px; }
  th { color:var(--muted); font-weight:500; }
  code { background:#151a26; padding:2px 6px; border-radius:6px; }
  .ok { color:var(--ok); }
  .warn { color:var(--warn); }
  .keyline { font-family:ui-monospace,monospace; background:#0b0e14; border:1px solid var(--line); padding:10px 12px; border-radius:8px; margin-top:8px; }
  .hide { display:none; }
</style>
</head>
<body>
<aside>
  <h2>Seller</h2>
  <div class="nav">
    <button class="on" data-tab="dash">Dashboard</button>
    <button data-tab="licenses">Licenses</button>
    <button data-tab="users">Users</button>
  </div>
</aside>
<main>
  <h1>FiveM</h1>
  <p class="sub">Private seller panel. Keys, durations, and live user IPs.</p>
  <section id="dash">
    <div class="card">
      <h3 style="margin:0 0 10px">Upload product file here</h3>
      <p class="sub">This is the .exe customers get when they click Play.</p>
      <div class="row">
        <label>Choose file
          <input id="pfile" type="file"/>
        </label>
        <button class="act" id="upfile">Save file</button>
      </div>
      <p class="sub" id="pfileinfo">No file yet</p>
    </div>
    <div class="stats">
      <div class="stat"><span>Users</span><b id="sUsers">0</b></div>
      <div class="stat"><span>Unused keys</span><b id="sUnused">0</b></div>
      <div class="stat"><span>Redeemed</span><b id="sUsed">0</b></div>
      <div class="stat"><span>Lifetime</span><b id="sLife">0</b></div>
    </div>
  </section>
  <section id="licenses" class="hide">
    <div class="card">
      <div class="row">
        <label>Product
          <select id="product"></select>
        </label>
        <label>Duration
          <select id="duration"></select>
        </label>
        <label>Amount
          <input id="count" type="number" min="1" max="50" value="1"/>
        </label>
        <label>Note
          <input id="note" placeholder="optional"/>
        </label>
        <button class="act" id="gen">Generate</button>
        <button class="ghost" id="reload">Refresh</button>
      </div>
    </div>
    <div class="card">
      <table>
        <thead><tr><th>Key</th><th>Product</th><th>Duration</th><th>Status</th><th>Expires</th><th>Used by</th><th>Created</th><th></th></tr></thead>
        <tbody id="keys"></tbody>
      </table>
    </div>
  </section>
  <section id="users" class="hide">
    <div class="card">
      <table>
        <thead><tr><th>User</th><th>Last IP</th><th>Bound IP</th><th>HWID</th><th>Sub</th><th>Expires</th><th>Last seen</th><th></th></tr></thead>
        <tbody id="usersBody"></tbody>
      </table>
    </div>
  </section>
</main>
<script>
const D = ${JSON.stringify(DURATIONS)};
const P = ${JSON.stringify(PRODUCTS)};
function esc(s){return String(s??"").replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function fmt(v){ if(!v) return "—"; const d=new Date(v); return isNaN(d) ? esc(v) : d.toLocaleString(); }
async function api(path,opt){
  const r=await fetch(path,Object.assign({headers:{"content-type":"application/json"}},opt||{}));
  if(!r.ok) throw new Error("fail");
  return r.json();
}
document.getElementById("product").innerHTML=P.map(p=>"<option>"+esc(p)+"</option>").join("");
document.getElementById("duration").innerHTML=D.map(d=>"<option value='"+d.code+"'"+(d.code==="30d"?" selected":"")+">"+esc(d.label)+"</option>").join("");
document.querySelectorAll(".nav button").forEach(b=>b.onclick=()=>{
  document.querySelectorAll(".nav button").forEach(x=>x.classList.remove("on"));
  b.classList.add("on");
  ["dash","licenses","users"].forEach(id=>document.getElementById(id).classList.toggle("hide", b.dataset.tab!==id));
});
function keyStatus(k){
  if(k.cancelled) return "<span class='bad'>cancelled</span>";
  if(k.redeemed) return "<span class='ok'>used</span>";
  return "<span class='warn'>unused</span>";
}
async function load(){
  const data=await api("/admin/api/state");
  document.getElementById("sUsers").textContent=data.stats.users;
  document.getElementById("sUnused").textContent=data.stats.unused;
  document.getElementById("sUsed").textContent=data.stats.redeemed;
  document.getElementById("sLife").textContent=data.stats.lifetime;
  const pf=data.productFile||{};
  document.getElementById("pfileinfo").textContent=pf.name?("Current file: "+pf.name):"No file yet";
  document.getElementById("usersBody").innerHTML=(data.users||[]).map(u=>
    "<tr><td>"+esc(u.name)+(u.banned?" <span class='bad'>banned</span>":"")+"</td><td><code>"+esc(u.last_ip)+"</code></td><td><code>"+esc(u.ip)+"</code></td><td><code>"+esc(u.hwid)+"</code></td><td>"+esc(u.sub)+"</td><td>"+(u.lifetime?"Lifetime":fmt(u.expires))+"</td><td>"+fmt(u.last_seen)+"</td><td class='acts'>"+
    "<button class='ghost tiny' data-act='ban-user' data-id='"+esc(u.id)+"' data-banned='"+(u.banned?"0":"1")+"'>"+(u.banned?"Unban":"Ban")+"</button>"+
    "<button class='danger tiny' data-act='del-user' data-id='"+esc(u.id)+"'>Delete</button></td></tr>"
  ).join("")||"<tr><td colspan=8>No users</td></tr>";
  document.getElementById("keys").innerHTML=(data.keys||[]).map(k=>{
    const full=k.key||k.prefix||"";
    return "<tr><td><div class='keycell'><code>"+esc(full)+"</code><button class='ghost tiny' data-act='copy-key' data-key='"+esc(full)+"'>Copy</button></div></td><td>"+esc(k.product)+"</td><td>"+esc(k.duration)+"</td><td>"+keyStatus(k)+"</td><td>"+(k.lifetime?"Lifetime":fmt(k.expires))+"</td><td>"+esc(k.redeemed_by||"—")+"</td><td>"+fmt(k.created)+"</td><td class='acts'>"+
    (k.cancelled?"":"<button class='ghost tiny' data-act='cancel-key' data-id='"+esc(k.id)+"'>Cancel</button>")+
    "<button class='danger tiny' data-act='del-key' data-id='"+esc(k.id)+"'>Delete</button></td></tr>";
  }).join("")||"<tr><td colspan=8>No keys</td></tr>";
}
document.getElementById("reload").onclick=()=>load().catch(()=>{});
document.getElementById("gen").onclick=async()=>{
  const body={
    count:Number(document.getElementById("count").value||1),
    product:document.getElementById("product").value,
    duration:document.getElementById("duration").value,
    note:document.getElementById("note").value
  };
  const data=await api("/admin/api/keys",{method:"POST",body:JSON.stringify(body)});
  await load();
  const made=data.keys||[];
  if(made[0]) navigator.clipboard.writeText(made.length===1?made[0]:made.join("\\n")).catch(()=>{});
};
document.getElementById("upfile").onclick=async()=>{
  const f=document.getElementById("pfile").files[0];
  if(!f){ alert("Pick a file first"); return; }
  const fd=new FormData();
  fd.append("file", f);
  fd.append("product","FiveM");
  const r=await fetch("/admin/api/product-file",{method:"POST",body:fd});
  if(!r.ok){ alert("Upload failed"); return; }
  await load();
};
document.addEventListener("click", async (e)=>{
  const b=e.target.closest("[data-act]");
  if(!b) return;
  const act=b.dataset.act, id=b.dataset.id;
  try{
    if(act==="copy-key"){
      const t=b.dataset.key||"";
      try{ await navigator.clipboard.writeText(t); }
      catch(_){
        const i=document.createElement("textarea");
        i.value=t; document.body.appendChild(i); i.select();
        document.execCommand("copy"); i.remove();
      }
      b.textContent="Copied";
      setTimeout(()=>{ b.textContent="Copy"; }, 900);
      return;
    } else if(act==="cancel-key"){
      if(!confirm("Cancel this key? It can never be redeemed, and the user's product is removed if it was already used.")) return;
      await api("/admin/api/keys/"+id+"/cancel",{method:"POST",body:"{}"});
    } else if(act==="del-key"){
      if(!confirm("Delete this key forever?")) return;
      await api("/admin/api/keys/"+id,{method:"DELETE"});
    } else if(act==="ban-user"){
      const banned=b.dataset.banned==="1";
      if(!confirm(banned?"Ban this user? They will not be able to log in.":"Unban this user?")) return;
      await api("/admin/api/users/"+id+"/ban",{method:"POST",body:JSON.stringify({banned})});
    } else if(act==="del-user"){
      if(!confirm("Delete this account forever?")) return;
      await api("/admin/api/users/"+id,{method:"DELETE"});
    } else return;
    await load();
  }catch(err){ alert("Action failed"); }
});
load().catch(()=>{ document.querySelector("main").innerHTML="<p>Failed to load</p>"; });
</script>
</body>
</html>`;

export function registerAdminRoutes(app) {
  app.get("/admin", (req, res) => {
    if (!isAdminRequest(req)) return deny(req, res);
    if (adminSecretOk(req)) setAdminCookie(res);
    if (req.query.k || req.query.secret) {
      return res.redirect(302, "/admin");
    }
    res.set("Cache-Control", "no-store");
    res.set("X-Robots-Tag", "noindex, nofollow");
    res.type("html").send(PAGE);
  });

  app.get("/admin/api/state", requireAdmin, async (_req, res) => {
    const users = await pool.query(
      `SELECT id, email, bind_ip, last_seen_ip, bind_hwid, created_at, last_seen_at,
              sub_product, sub_expires_at, sub_lifetime, banned
       FROM users ORDER BY COALESCE(last_seen_at, created_at) DESC LIMIT 300`
    );
    const keys = await pool.query(
      `SELECT k.id, k.prefix, k.product, k.duration_code, k.redeemed_at, k.cancelled_at,
              k.created_at, k.expires_at, u.email AS redeemed_by
       FROM license_keys k
       LEFT JOIN users u ON u.id = k.redeemed_by
       ORDER BY k.created_at DESC LIMIT 300`
    );
    const unused = keys.rows.filter((r) => !r.redeemed_at && !r.cancelled_at).length;
    const redeemed = keys.rows.filter((r) => r.redeemed_at && !r.cancelled_at).length;
    const lifetime = users.rows.filter((r) => r.sub_lifetime && !r.banned).length;
    const fileRow = await pool.query(`SELECT filename, version, octet_length(data) AS bytes FROM product_files WHERE product = 'FiveM'`);
    res.json({
      stats: { users: users.rowCount, unused, redeemed, lifetime },
      productFile: fileRow.rows[0] ? { name: fileRow.rows[0].filename, version: fileRow.rows[0].version, size: fileRow.rows[0].bytes } : null,
      users: users.rows.map((r) => ({
        id: r.id,
        name: r.email,
        ip: r.bind_ip || "",
        last_ip: r.last_seen_ip || r.bind_ip || "",
        hwid: r.bind_hwid ? String(r.bind_hwid).slice(0, 12) + "…" : "",
        sub: r.sub_product || "—",
        expires: r.sub_expires_at,
        lifetime: Boolean(r.sub_lifetime),
        last_seen: r.last_seen_at,
        banned: Boolean(r.banned),
      })),
      keys: keys.rows.map((r) => {
        const d = durationByCode(r.duration_code);
        return {
          id: r.id,
          prefix: String(r.prefix || "").replace(/-+$/g, ""),
          key: String(r.prefix || "").replace(/-+$/g, ""),
          product: r.product || "FiveM",
          duration: d.label,
          redeemed: Boolean(r.redeemed_at),
          cancelled: Boolean(r.cancelled_at),
          redeemed_by: r.redeemed_by || "",
          created: r.created_at,
          expires: r.expires_at,
          lifetime: d.seconds == null,
        };
      }),
    });
  });

  app.post("/admin/api/keys", requireAdmin, async (req, res) => {
    let count = Number(req.body?.count || 1);
    if (!Number.isFinite(count) || count < 1) count = 1;
    if (count > 50) count = 50;
    const product = PRODUCTS.includes(req.body?.product) ? req.body.product : "FiveM";
    const dur = durationByCode(req.body?.duration);
    const note = String(req.body?.note || "").slice(0, 80);
    const made = [];
    for (let i = 0; i < count; i++) {
      const key = makeKey(product);
      await pool.query(
        `INSERT INTO license_keys (key_hash, prefix, created_ip, product, duration_code, duration_seconds, note)
         VALUES ($1, $2, $3, $4, $5, $6, $7)`,
        [hashKey(key), key, clientIp(req), product, dur.code, dur.seconds, note || null]
      );
      made.push(key);
    }
    res.json({ keys: made });
  });

  app.post("/admin/api/product-file", requireAdmin, upload.single("file"), async (req, res) => {
    try {
      if (!req.file || !req.file.buffer || !req.file.buffer.length) {
        return res.status(400).json({ message: "No file" });
      }
      const product = PRODUCTS.includes(req.body?.product) ? req.body.product : "FiveM";
      const filename = String(req.file.originalname || "product.exe").replace(/[^\w.\-]+/g, "_").slice(0, 80);
      const version = String(Date.now());
      await pool.query(
        `INSERT INTO product_files (product, filename, version, data, updated_at)
         VALUES ($1, $2, $3, $4, NOW())
         ON CONFLICT (product) DO UPDATE SET filename = $2, version = $3, data = $4, updated_at = NOW()`,
        [product, filename, version, req.file.buffer]
      );
      return res.json({ ok: true, name: filename, version });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.post("/admin/api/keys/:id/cancel", requireAdmin, async (req, res) => {
    const id = uuid(req.params.id);
    if (!id) return res.status(400).json({ message: "Invalid id" });
    try {
      const found = await pool.query(
        `SELECT id, redeemed_by, cancelled_at FROM license_keys WHERE id = $1`,
        [id]
      );
      const key = found.rows[0];
      if (!key) return res.status(404).json({ message: "Key not found" });
      if (!key.cancelled_at) {
        await pool.query(`UPDATE license_keys SET cancelled_at = NOW() WHERE id = $1`, [id]);
        await clearSub(key.redeemed_by);
      }
      return res.json({ ok: true });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.delete("/admin/api/keys/:id", requireAdmin, async (req, res) => {
    const id = uuid(req.params.id);
    if (!id) return res.status(400).json({ message: "Invalid id" });
    try {
      const found = await pool.query(
        `SELECT id, redeemed_by FROM license_keys WHERE id = $1`,
        [id]
      );
      const key = found.rows[0];
      if (!key) return res.status(404).json({ message: "Key not found" });
      await clearSub(key.redeemed_by);
      await pool.query(`DELETE FROM license_keys WHERE id = $1`, [id]);
      return res.json({ ok: true });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.post("/admin/api/users/:id/ban", requireAdmin, async (req, res) => {
    const id = uuid(req.params.id);
    if (!id) return res.status(400).json({ message: "Invalid id" });
    const banned = req.body?.banned !== false && req.body?.banned !== "0";
    try {
      const upd = await pool.query(`UPDATE users SET banned = $1 WHERE id = $2 RETURNING id`, [banned, id]);
      if (!upd.rowCount) return res.status(404).json({ message: "User not found" });
      return res.json({ ok: true, banned });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });

  app.delete("/admin/api/users/:id", requireAdmin, async (req, res) => {
    const id = uuid(req.params.id);
    if (!id) return res.status(400).json({ message: "Invalid id" });
    try {
      await pool.query(`UPDATE license_keys SET redeemed_by = NULL WHERE redeemed_by = $1`, [id]);
      const del = await pool.query(`DELETE FROM users WHERE id = $1 RETURNING id`, [id]);
      if (!del.rowCount) return res.status(404).json({ message: "User not found" });
      return res.json({ ok: true });
    } catch (err) {
      console.error(err);
      return res.status(500).json({ message: "Server error" });
    }
  });
}

export { hashKey };
