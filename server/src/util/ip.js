import { createHmac } from "crypto";

function strip(ip) {
  return String(ip || "")
    .trim()
    .replace(/^::ffff:/i, "");
}

export function isPrivateIp(ip) {
  const v = strip(ip);
  if (!v) return true;
  if (v === "127.0.0.1" || v === "::1" || v === "localhost") return true;
  if (v.startsWith("10.")) return true;
  if (v.startsWith("192.168.")) return true;
  if (v.startsWith("169.254.")) return true;
  const m = v.match(/^172\.(\d+)\./);
  if (m) {
    const n = Number(m[1]);
    if (n >= 16 && n <= 31) return true;
  }
  if (v.startsWith("fc") || v.startsWith("fd") || v.startsWith("fe80:")) return true;
  return false;
}

function candidates(req) {
  const headers = [
    req.headers["cf-connecting-ip"],
    req.headers["true-client-ip"],
    req.headers["x-real-ip"],
    req.headers["x-client-ip"],
    req.headers["x-forwarded-for"],
    req.ip,
  ];
  const out = [];
  for (const h of headers) {
    if (!h) continue;
    String(h)
      .split(",")
      .map((s) => strip(s))
      .filter(Boolean)
      .forEach((ip) => out.push(ip));
  }
  return out;
}

export function clientIp(req) {
  const list = candidates(req);
  const pub = list.find((ip) => !isPrivateIp(ip));
  return pub || list[0] || "";
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
