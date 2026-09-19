export function clientIp(req) {
  const raw = String(req.ip || "").split(",")[0].trim();
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
