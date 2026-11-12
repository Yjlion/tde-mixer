#!/bin/sh
# Runs every backend operation against a test sound server and checks the
# result with pactl.
#
#   tests/run-backend-tests.sh <backendtest binary> <pipewire|pulse> [server]
#
# server: pipewire (default) or pulse, which sound server to start. The
# pulse backend can be tested against both; the pipewire backend only
# against PipeWire.
set -u
BT=$1
BACKEND=$2
SERVER=${3:-pipewire}
HERE=$(cd "$(dirname "$0")" && pwd)

AUDIO_SERVER=$SERVER
. "$HERE/../tools/audio-test-env.sh"
start_test_streams

FAILED=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; FAILED=$((FAILED + 1)); }

# field of the sink input of an application: volume (percent of the first
# channel), mute (yes/no) or sink (name)
input_field() { # app field
	pactl list sink-inputs | awk -v app="$1" -v field="$2" '
		/^Sink Input #/ { if (found) exit; vol = ""; mute = ""; sink = "" }
		/^\tSink: / { sink = $2 }
		/^\tMute: / { mute = $2 }
		/^\tVolume: / { for (i = 1; i <= NF; i++) if ($i ~ /%$/) { vol = $i; sub("%", "", vol); break } }
		index($0, "application.name = \"" app "\"") { found = 1 }
		END { if (field == "volume") print vol; else if (field == "mute") print mute; else print sink }'
}
sink_name() { pactl list short sinks | awk -v i="$1" '$1 == i { print $2 }'; }
sink_volume() { pactl get-sink-volume "$1" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /%$/) { sub("%", "", $i); print $i; exit } }'; }
sink_mute() { pactl get-sink-mute "$1" | awk '{ print $2 }'; }
default_sink() { pactl info | sed -n 's/^Default Sink: //p'; }
check() { # description expected actual
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}

echo "== $BACKEND backend on $SERVER"
"$BT" "$BACKEND" stream-volume "Music Player" 35
check "stream volume" 35 "$(input_field "Music Player" volume)"
"$BT" "$BACKEND" stream-mute "Music Player" 1
check "stream mute" yes "$(input_field "Music Player" mute)"
"$BT" "$BACKEND" stream-mute "Music Player" 0
check "stream unmute" no "$(input_field "Music Player" mute)"
"$BT" "$BACKEND" move "Web Browser" hdmi
sleep 1
check "move stream" hdmi "$(sink_name "$(input_field "Web Browser" sink)")"
"$BT" "$BACKEND" move "Web Browser" speakers
sleep 1
check "move stream back" speakers "$(sink_name "$(input_field "Web Browser" sink)")"
"$BT" "$BACKEND" device-volume hdmi 55
check "device volume" 55 "$(sink_volume hdmi)"
"$BT" "$BACKEND" device-mute hdmi 1
check "device mute" yes "$(sink_mute hdmi)"
"$BT" "$BACKEND" device-mute hdmi 0
check "device unmute" no "$(sink_mute hdmi)"
"$BT" "$BACKEND" default hdmi
sleep 1
check "default device" hdmi "$(default_sink)"
"$BT" "$BACKEND" default speakers
sleep 1
check "default device back" speakers "$(default_sink)"

# level meters: the playing streams must show a signal
PEAKS=$("$BT" "$BACKEND" peaks)
echo "$PEAKS" | sed 's/^/    /'
P=$(echo "$PEAKS" | awk '($1 == "Notifications" || $1 == "Web") && $NF > 0.05 { ok = 1 } END { print ok ? "yes" : "no" }')
check "stream level meter" yes "$P"
P=$(echo "$PEAKS" | awk '$1 == "speakers" && $2 > 0.05 { ok = 1 } END { print ok ? "yes" : "no" }')
check "device level meter" yes "$P"

# reconnect after the server restarts
"$BT" "$BACKEND" wait-reconnect > /tmp/reconnect.log 2>&1 &
WAITER=$!
sleep 3
if [ "$SERVER" = pulse ]; then
	pulseaudio -k; sleep 1
	pulseaudio --daemonize=yes --exit-idle-time=-1 -n \
		-L "module-native-protocol-unix auth-anonymous=1" -L module-always-sink
else
	pkill -x pipewire-pulse; pkill -x wireplumber; pkill -x pipewire; sleep 1
	(pipewire >/tmp/pipewire.log 2>&1 &); sleep 1
	(wireplumber >/tmp/wireplumber.log 2>&1 &)
	(pipewire-pulse >/tmp/pipewire-pulse.log 2>&1 &)
fi
if wait $WAITER; then pass "reconnect ($(cat /tmp/reconnect.log))"; else fail "reconnect: $(cat /tmp/reconnect.log)"; fi

echo "== $FAILED failed"
[ $FAILED -eq 0 ]
