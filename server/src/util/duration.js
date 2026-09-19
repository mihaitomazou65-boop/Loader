export const PRODUCTS = ["FiveM"];

export const DURATIONS = [
  { code: "1h", label: "1 Hour", seconds: 3600 },
  { code: "6h", label: "6 Hours", seconds: 21600 },
  { code: "12h", label: "12 Hours", seconds: 43200 },
  { code: "1d", label: "1 Day", seconds: 86400 },
  { code: "3d", label: "3 Days", seconds: 86400 * 3 },
  { code: "7d", label: "1 Week", seconds: 86400 * 7 },
  { code: "14d", label: "2 Weeks", seconds: 86400 * 14 },
  { code: "30d", label: "1 Month", seconds: 86400 * 30 },
  { code: "90d", label: "3 Months", seconds: 86400 * 90 },
  { code: "180d", label: "6 Months", seconds: 86400 * 180 },
  { code: "365d", label: "1 Year", seconds: 86400 * 365 },
  { code: "lifetime", label: "Lifetime", seconds: null },
];

export function durationByCode(code) {
  return DURATIONS.find((d) => d.code === code) || DURATIONS.find((d) => d.code === "30d");
}
