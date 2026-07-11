# On-hardware verification checklist

Manual pass on a real EE02 before tagging a release. Monitor at 115200.

## Portal basics
- [ ] KEY1 from sleep: 2 ack blinks, status page draws (URL, IP, QR, legend)
- [ ] QR code scans from a phone and opens the portal
- [ ] `http://<name>.local` and the numeric IP both serve the form
- [ ] Form shows current values; Save with a bad name shows the error and changes nothing (power-cycle → old values)
- [ ] Valid Save: confirmation page, panel fetches a new photo, device sleeps
- [ ] KEY1 while portal is up: exits, fetches, sleeps
- [ ] 10-minute idle: same exit path (check serial log for "idle timeout")

## Settings behavior
- [ ] Interval 15 min: next timer wake ~15 min later (serial timestamps)
- [ ] Orientation landscape: photo fetches at 1600×1200 and renders correctly; status page centered
- [ ] Quiet hours spanning now: sleep log shows extended sleep to window end
- [ ] Manual timezone: fetch log shows no ip-api call; times correct
- [ ] Paused checkbox: equals KEY3 (timer wakes take the quick-sleep path)
- [ ] Fetch-new-picture button: new photo appears
- [ ] Forget Wi-Fi: EE02-Setup hotspot reopens with instructions
- [ ] Unplug the router, wait for a scheduled refresh: photo stays, serial logs "keeping photo"; press KEY2: centered error screen appears

## Regressions
- [ ] KEY2: 1 blink, new photo
- [ ] KEY3: 2/1 blinks, pin/unpin; pinned timer wake stays asleep (log)
- [ ] Cold boot with no Wi-Fi saved: provisioning screen with two scannable QR codes; hotspot QR joins, portal QR opens the page
- [ ] Dev mode: plugged into a computer — stays awake, buttons polled, KEY1 portal session works, portal reachable without pressing KEY1, saving from it fetches a new photo, unplug → sleeps
- [ ] Battery wake after unplugging: sleeps normally (no dev-mode leak)
