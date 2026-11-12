#!/bin/bash
# Capture the screenshots in screenshots/ and doc/en/ under Xvfb with the
# TDE window manager and panel.
#
# Prerequisites: a Debian container with the TDE packages (kcontrol, kicker,
# twin, khelpcenter, tde-systemsettings, kmix), tde-mixer installed, Xvfb on
# :1, xdotool, ImageMagick and the sound server set up by
# tools/audio-test-env.sh (PipeWire) with start_test_streams running.
export DISPLAY=:1 PATH=/opt/trinity/bin:$PATH HOME=/root
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/runtime-0}
export XDG_CONFIG_DIRS=/opt/trinity/etc/xdg:/etc/xdg XDG_DATA_DIRS=/opt/trinity/share:/usr/share
OUT=${OUT:-/shots}; mkdir -p $OUT
CFG=$HOME/.trinity/share/config

pkill -x tde-mixer; pkill -x tdecmshell; pkill -x kcontrol; pkill -x systemsettings; pkill -x tde-mixer-tray
sleep 1
pgrep -x twin >/dev/null || (twin >/dev/null 2>&1 &)
xsetroot -solid "#3a6ea5"
sleep 2

shot() { # name window-pattern
	local id
	for i in $(seq 1 30); do id=$(xdotool search --onlyvisible --name "$2" | tail -1); [ -n "$id" ] && break; sleep 0.5; done
	if [ -z "$id" ]; then echo "MISSING window for $1"; return; fi
	sleep 1.5
	timeout 20 import -frame -window "$id" "$OUT/$1.png" && echo "saved $1"
}
key() { xdotool key --clearmodifiers "$@"; sleep 1.2; }
sizewin() { # pattern w h
	local id; id=$(xdotool search --onlyvisible --name "$1" | tail -1)
	xdotool windowmove "$id" 40 30; xdotool windowsize "$id" "$2" "$3"; sleep 1
}
app() { # page [backend]
	pkill -x tde-mixer; sleep 1
	(tde-mixer --page "$1" ${2:+--backend $2} >/dev/null 2>&1 &)
	sleep 4; sizewin "Volume Mixer" 780 560
}

# a realistic mix: the browser on the HDMI monitor, other volumes varied
pactl set-sink-volume speakers 65%
pactl set-sink-volume hdmi 80%
pactl set-source-volume microphone 70%
input() { pactl list sink-inputs | awk -v app="$1" '/^Sink Input #/{id=substr($3,2)} index($0,"application.name = \"" app "\""){print id; exit}'; }
pactl set-sink-input-volume "$(input "Music Player")" 70%
pactl set-sink-input-volume "$(input "Notifications")" 45%
pactl move-sink-input "$(input "Web Browser")" hdmi
sleep 1

app playback;      shot playback "Volume Mixer"
app recording;     shot recording "Volume Mixer"
app output;        shot output-devices "Volume Mixer"
app input;         shot input-devices "Volume Mixer"
app output pulse;  shot pulse-backend "Volume Mixer"
pkill -x tde-mixer; sleep 1

# Control Center module, Settings tab
(tdecmshell mixer >/dev/null 2>&1 &); sleep 5
sizewin "Control Module" 820 600
key alt+s
shot settings "Control Module"
pkill -x tdecmshell; sleep 1

# Trinity Control Center: Sound & Multimedia -> Volume Mixer
(kcontrol >/dev/null 2>&1 &); sleep 8
W=$(xdotool search --onlyvisible --name "Control Center" | tail -1)
xdotool windowmove $W 0 0; xdotool windowsize $W 1100 760; sleep 1
xdotool mousemove 16 229 click 1; sleep 1.5; xdotool mousemove 118 349 click 1; sleep 5
shot kcontrol "Control Center"
pkill -x kcontrol; sleep 1

# System Settings: Computer Administration -> Volume Mixer (default window
# size, which the click positions depend on)
rm -f $CFG/systemsettingsrc
(systemsettings >/dev/null 2>&1 &); sleep 8
W=$(xdotool search --onlyvisible --name "System Settings" | tail -1)
xdotool windowmove $W 0 0; sleep 1
xdotool mousemove 610 457 click 1; sleep 0.5; xdotool click 1; sleep 0.5; xdotool click 1; sleep 1
shot systemsettings-overview "System Settings"
xdotool mousemove 68 430 click 1; sleep 5
shot systemsettings "System Settings"
pkill -x systemsettings; sleep 1

# the handbook in KHelpCenter
(khelpcenter help:/tde-mixer/using.html >/dev/null 2>&1 &); sleep 8
sizewin "Help Center" 1000 720; sleep 2
shot handbook-khelpcenter "Help Center"
pkill -x khelpcenter; sleep 1

# tray icon in kicker, with KMix set to start at login
rm -f $CFG/tde-mixer-trayrc $CFG/kmixrc
pgrep -x kicker >/dev/null || (kicker >/dev/null 2>&1 &); sleep 8
(tde-mixer-tray >/dev/null 2>&1 &); sleep 6
# the KMix question: the dialog, not the tray icon of the same name
D=""
for i in $(seq 1 20); do
	for w in $(xdotool search --onlyvisible --name "^Volume Control$"); do
		h=$(xdotool getwindowgeometry "$w" | awk '/Geometry/ { split($2, a, "x"); print a[2] }')
		[ "${h:-0}" -gt 100 ] && [ "${h:-0}" -lt 400 ] && D=$w
	done
	[ -n "$D" ] && break; sleep 0.5
done
if [ -n "$D" ]; then
	sleep 1; import -frame -window "$D" "$OUT/kmix-question.png" && echo "saved kmix-question"
	xdotool windowactivate "$D"; sleep 0.5; key alt+k
else
	echo "MISSING window for kmix-question"
fi
# the icon is the last item before the clock
TX=1143; TY=884
xdotool mousemove $((TX - 3)) $((TY - 3)); sleep 0.3; xdotool mousemove $TX $TY; sleep 3
import -window root -crop 360x150+920+750 +repage "$OUT/tray-tooltip.png" && echo "saved tray-tooltip"
xdotool mousemove $TX $TY click 1; sleep 2
import -window root -crop 520x460+760+440 +repage "$OUT/tray-popup.png" && echo "saved tray-popup"
key Escape
xdotool mousemove $TX $TY click 3; sleep 1.5
# open the Output Device submenu
xdotool mousemove 880 742; sleep 2
import -window root -crop 760x460+520+440 +repage "$OUT/tray-menu.png" && echo "saved tray-menu"
key Escape Escape
