.pragma library

var workspace = "#101317"
var surface = "#171b20"
var elevated = "#1c2127"
var inset = "#12161a"
var separator = "#34404a"
var separatorStrong = "#4a5965"
var textPrimary = "#edf2f5"
var textSecondary = "#b6c0c8"
var textMuted = "#7f8c97"
var healthy = "#72b48b"
var information = "#78a9c9"
var warning = "#d4a65a"
var critical = "#d67979"
var unknown = "#94a7b5"
var disabled = "#59636d"
var hover = "#27313a"
var pressed = "#0e6d84"
var focus = "#61b8d5"
var selected = "#155469"
var readOnly = "#244a58"
var mono = "Consolas"
var ui = "Segoe UI"

function tone(name) {
    if (name === "healthy") return healthy
    if (name === "warning") return warning
    if (name === "fault" || name === "critical") return critical
    if (name === "running" || name === "information") return information
    if (name === "limited" || name === "unknown") return unknown
    return textMuted
}
