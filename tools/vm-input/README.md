# Driving the dev VM with real pointer events

`ydotool` cannot click on FinnOS windows in the VM. Its uinput device is tagged
`ID_INPUT_KEYBOARD` (no `ID_INPUT_MOUSE`), so libinput routes its key events but
drops its motion and button events. `ydotool mousemove --absolute` cannot work
either, because the device has no `ABS_X`/`ABS_Y` axes.

`uinput-pointer.py` creates a proper virtual mouse instead: `BTN_LEFT`/`RIGHT`/
`MIDDLE` plus `ABS_X`/`ABS_Y` with the range set to the output size. udev then
tags the device `ID_INPUT_MOUSE=1`, libinput classifies it as a pointer, and
`finn-comp` picks it up through its udev hotplug path (no compositor restart is
needed). wlroots warps the cursor from the normalised absolute axes and sends
the button through the normal seat path, so clicks reach native Wayland clients
exactly as physical clicks do.

## Run it on the VM

The script lives in the synced tree at `~/finnos/tools/vm-input/uinput-pointer.py`,
but a copy under `/tmp` is fine too. The server needs root only to create the
uinput device; clients talk to it over a world-writable unix socket.

```sh
# once, on the VM (background server):
sudo setsid python3 ~/finnos/tools/vm-input/uinput-pointer.py serve \
    --width 1440 --height 900 > /tmp/finn-uinput.log 2>&1 < /dev/null &

# from any user on the VM:
python3 ~/finnos/tools/vm-input/uinput-pointer.py send 'abs 700 400'   # warp
python3 ~/finnos/tools/vm-input/uinput-pointer.py send 'down left'     # press
python3 ~/finnos/tools/vm-input/uinput-pointer.py send 'up left'       # release
python3 ~/finnos/tools/vm-input/uinput-pointer.py send 'click 700 400 left'
python3 ~/finnos/tools/vm-input/uinput-pointer.py send 'quit'          # tear down
```

`rel DX DY` emits relative motion and `tap` is an alias for `click`. Each
command ends with `EV_SYN/SYN_REPORT`; `click` inserts a short hold between
press and release. Coordinates are layout pixels (0..width, 0..height).

Verify the device and compositor with:

```sh
udevadm info /dev/input/event4 | grep ID_INPUT_MOUSE
sudo ls -l /proc/$(pgrep finn-comp)/fd | grep /dev/input
tail -f /tmp/finn-uinput.log
```

Keyboard injection still uses ydotoold (its keyboard device works):

```sh
sudo -u finn env YDOTOOL_SOCKET=/tmp/.ydotool_socket ydotool type 'text'
```
