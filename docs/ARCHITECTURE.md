# Transformant Top 3 — architecture

Hardware: Waveshare ESP32-S3 1.28-inch round touch LCD (240x240). Verify exact board revision and display controller before selecting driver/pin mapping.

Display: top three consultants by billable amount for the current calendar month, with names, SEK amounts, data-through date and stale-data indicator. No settings on display.

Local admin: http://tfmtop.local/ (mDNS). Password-protected UI with Wi-Fi scanning, SSID selection, brightness, refresh interval, API configuration, diagnostics and OTA firmware updates.

Data: ESP32 -> authenticated HTTPS backend -> Microsoft Dynamics 365 Business Central. Backend handles OAuth client credentials and stores secrets securely. Never store a BC client secret in firmware, GitHub, browser local storage, or ESP32 configuration.

Date range: first day of current month through latest available posted/registered date; timezone Europe/Stockholm. Decide whether ranking is invoiced amounts or billable time x rate after reviewing BC data model.

Diagnostics: correlation ID across device/backend, Wi-Fi/DNS/TLS/backend/OAuth/BC status, HTTP code, latency, JSON validation, cache age, last success, actionable errors. Redact secrets and tokens.

Fallback: show cached last successful leaderboard marked stale; Wi-Fi setup AP if credentials fail. Secure admin access and OTA integrity checks required before deployment.
