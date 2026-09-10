# On-hardware verification checklist

Manual pass on a real EE02 before tagging a release. Monitor at 115200.
The EE02 is the only supported board.

## Status page (redesigned — fixed 12-column grid)
- [ ] KEY1 from sleep: 2 ack blinks, status page draws
- [ ] Rounded **window frame** is visible, centred on the panel
- [ ] Header: `Paperframe` + `Firmware build <N>` (matches `/log`'s build number)
- [ ] All four zone rules line up with the content-box edges; legend thirds
      align to the grid
- [ ] **Six-colour swatch** shows six visibly distinct cells (black, white,
      yellow, red, blue, green)
- [ ] QR scans from a phone and opens the portal
- [ ] `http://<name>.local` and the `Or http://<ip>` line both serve the form;
      the IP matches `/debug`
- [ ] `Device ID` shows `PF-XXXX` and matches the low 16 bits of the MAC on `/debug`
- [ ] "Next image refresh" shows a relative value (`in Xh Ym`); after one
      sleep/wake cycle it has counted down
- [ ] Quiet hours spanning the next wake → value reads `Paused until HH:00`
- [ ] KEY3 (pin) → value reads `Pinned`

## Portal
- [ ] KEY1 while portal is up: exits, fetches, sleeps
- [ ] 10-minute idle: same exit path (serial log "idle timeout")
- [ ] Per-field auto-save: change a field, see `saved ✓`; power-cycle → persisted
- [ ] Bad device name: inline error, nothing saved
- [ ] Changing the image URL fetches + redraws; changing name/timezone does not
- [ ] Advanced → Factory reset: wipes settings + Wi-Fi, reboots to onboarding

## Settings behavior
- [ ] Interval 15 min: next timer wake ~15 min later (serial timestamps)
- [ ] Orientation landscape: image fetches at 1600×1200 and renders correctly;
      status page still portrait
- [ ] Quiet hours spanning now: sleep log shows extended sleep to window end
- [ ] Manual timezone: fetch log shows no ip-api call; times correct
- [ ] Paused: equals KEY3 (timer wakes take the quick-sleep path)

## Onboarding & error
- [ ] Cold boot / factory reset: onboarding screen shows `Join "Paperframe-Setup"`,
      QR joins the hotspot, portal opens, `paperframe.local` resolves after setup
- [ ] Unplug the router, wait for a scheduled refresh: image stays, serial logs
      "keeping image"; press KEY2 → error screen on the same grid, failure
      reason in the action line, legend shows `Status / Retry / Pin image`

## Regressions
- [ ] KEY2: 1 blink, new image
- [ ] KEY3: 2/1 blinks, pin/unpin; pinned timer wake stays asleep (log)
- [ ] Dev mode: plugged into a computer — stays awake, buttons polled, KEY1
      portal session works, portal reachable without pressing KEY1, unplug → sleeps
- [ ] Battery wake after unplugging: sleeps normally (no dev-mode leak)

## OTA (unchanged)
- [ ] `/debug` still shows the git hash
- [ ] `/ota/peek` reports a build-number comparison
- [ ] A forced `/ota/install` still starts
