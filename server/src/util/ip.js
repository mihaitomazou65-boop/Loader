import { createHmac } from "crypto";

export function clientIp(req) {
  const fwd = String(req.headers["x-forwarded-for"] || "")
    .split(",")[0]
    .trim();
  const raw = (fwd || String(req.ip || "")).split(",")[0].trim();
  return raw.replace(/^::ffff:/i, "");
}

export function adminIps() {
  const raw = String(process.env.ADMIN_ALLOW_IP || "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  return raw;
}

export function isAdminIp(req) {
  const allowed = adminIps();
  if (!allowed.length) return false;
  const ip = clientIp(req);
  return allowed.includes(ip);
}

function adminCookieValue() {
  const secret = process.env.ADMIN_SECRET || "";
  if (secret.length < 8) return "";
  return createHmac("sha256", process.env.JWT_SECRET || "loader")
    .update(secret)
    .digest("hex");
}

function readCookie(req, name) {
  const raw = String(req.headers.cookie || "");
  const parts = raw.split(";").map((s) => s.trim());
  for (const p of parts) {
    const i = p.indexOf("=");
    if (i === -1) continue;
    if (p.slice(0, i) === name) return decodeURIComponent(p.slice(i + 1));
  }
  return "";
}

export function adminSecretOk(req) {
  const secret = process.env.ADMIN_SECRET || "";
  if (secret.length < 8) return false;
  const q = String(req.query?.k || req.query?.secret || req.headers["x-admin-secret"] || "");
  if (q && q === secret) return true;
  const cookie = adminCookieValue();
  return cookie && readCookie(req, "ladm") === cookie;
}

export function isAdminRequest(req) {
  return isAdminIp(req) || adminSecretOk(req);
}

export function setAdminCookie(res) {
  const v = adminCookieValue();
  if (!v) return;
  res.setHeader(
    "Set-Cookie",
    `ladm=${v}; Path=/admin; HttpOnly; Secure; SameSite=Strict; Max-Age=604800`
  );
}

export { adminCookieValue };
