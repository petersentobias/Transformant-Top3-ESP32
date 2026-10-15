# Implementation plan

- [ ] Verify exact Waveshare board variant, LCD driver, GPIO mapping, backlight control
- [ ] Build PlatformIO firmware with 240x240 round leaderboard
- [ ] Build local authenticated admin interface and mDNS tfmtop.local
- [ ] Add Wi-Fi scan, provisioning AP and recovery
- [ ] Add backend URL and device credential configuration (no BC secret on device)
- [ ] Build secure backend integration with BC and month-to-date aggregation
- [ ] Add step-by-step diagnostics, masked export and correlation IDs
- [ ] Add cache, stale-data indicator, refresh every 5 minutes
- [ ] Add signed/validated OTA firmware update
- [ ] Add GitHub Actions firmware build and release artifact
