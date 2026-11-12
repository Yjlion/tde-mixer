#!/bin/sh
# Start a sound server with virtual devices and a few test streams, for
# trying tde-mixer in a container (screenshots, CI, development).
#
#   AUDIO_SERVER=pipewire . tools/audio-test-env.sh  # PipeWire + WirePlumber + pipewire-pulse
#   AUDIO_SERVER=pulse . tools/audio-test-env.sh     # a real PulseAudio daemon
#   start_test_streams                   # some applications playing / recording
#
# Needs: pipewire, wireplumber, pipewire-pulse (or pulseaudio), pulseaudio-utils,
# dbus, sox.
AUDIO_SERVER=${AUDIO_SERVER:-pipewire}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -u)}
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
	export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
	dbus-daemon --session --fork --address="$DBUS_SESSION_BUS_ADDRESS" >/dev/null 2>&1
fi

wait_for_server() {
	for i in $(seq 1 50); do pactl info >/dev/null 2>&1 && return 0; sleep 0.2; done
	echo "sound server did not start" >&2; return 1
}

if [ "$AUDIO_SERVER" = pulse ]; then
	pulseaudio --daemonize=yes --exit-idle-time=-1 --log-target=stderr -n \
		-L module-native-protocol-unix -L module-always-sink >/dev/null 2>&1
	wait_for_server
	pactl load-module module-null-sink sink_name=speakers \
		sink_properties=device.description=Speakers >/dev/null
	pactl load-module module-null-sink sink_name=hdmi \
		sink_properties=device.description=HDMI_Monitor >/dev/null
	pactl load-module module-null-source source_name=microphone \
		source_properties=device.description=Microphone >/dev/null 2>&1 || true
	pactl set-default-sink speakers
	pactl set-default-source microphone 2>/dev/null || true
else
	# WirePlumber 0.4 (Debian 12) quits when its Bluetooth monitor cannot
	# reach logind, as in a container; there is no Bluetooth here anyway
	mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/wireplumber/bluetooth.lua.d"
	echo 'bluez_monitor.enabled = false' \
		> "${XDG_CONFIG_HOME:-$HOME/.config}/wireplumber/bluetooth.lua.d/80-no-bluetooth.lua"
	(pipewire >/tmp/pipewire.log 2>&1 &)
	sleep 1
	(wireplumber >/tmp/wireplumber.log 2>&1 &)
	(pipewire-pulse >/tmp/pipewire-pulse.log 2>&1 &)
	wait_for_server
	mknode() { # name description class
		pw-cli create-node adapter "{ factory.name=support.null-audio-sink node.name=$1
			node.description=\"$2\" media.class=$3 audio.position=[ FL FR ]
			monitor.channel-volumes=true object.linger=true }" >/dev/null
	}
	mknode speakers "Speakers" Audio/Sink
	mknode hdmi "HDMI Monitor" Audio/Sink
	mknode microphone "Microphone" Audio/Source/Virtual
	sleep 1
	pactl set-default-sink speakers
	pactl set-default-source microphone
fi

start_test_streams() {
	[ -f /tmp/tone-a.wav ] || sox -n -r 48000 -c 2 /tmp/tone-a.wav synth 300 sine 330 sine 440 vol 0.5 2>/dev/null
	[ -f /tmp/tone-b.wav ] || sox -n -r 48000 -c 2 /tmp/tone-b.wav synth 300 pinknoise vol 0.2 2>/dev/null
	play_loop() { # file app icon media
		(while :; do paplay --property="application.name=$2" --property="application.icon_name=$3" \
			--property="media.name=$4" "$1" || sleep 1; done) >/dev/null 2>&1 &
	}
	play_loop /tmp/tone-a.wav "Music Player" "juk" "Ocean Waves - Track 3"
	play_loop /tmp/tone-b.wav "Web Browser" "konqueror" "Video - Rain sounds"
	play_loop /tmp/tone-a.wav "Notifications" "knotify" "Login sound"
	parecord --property="application.name=Voice Recorder" --property="application.icon_name=krec" \
		--property="media.name=Recording" /dev/null >/dev/null 2>&1 &
	sleep 2
}
