#!/usr/bin/env bash
# Sends Claude Code session status to the Agentur clock (custom firmware on the yellow wifi clock).
# The clock sits on an isolated hotspot on this PC, with the fixed IP 10.42.0.2.
#
# Usage from Claude Code hooks: agentur-status.sh prompt|tool|posttool|notify|stop|end
#
# Several Claude sessions share the clock. Each session stores its own state, and the clock shows
# the most important one: question > working > done > idle. Stale states don't count
# (an interrupted session that never said "done", old questions).
# Counters: w = working, q = needs an answer, v = finished in the last 15 minutes.
# n = timestamp in ms: the clock drops messages that arrive after a newer one
# (the calls run in the background and can arrive out of order).
#
# Never blocks: all networking happens in the background with a short timeout, and errors are ignored.

event="${1:-stop}"
input="$(cat 2>/dev/null)"
dir="${XDG_CACHE_HOME:-$HOME/.cache}/agentur"
mkdir -p "$dir/sessions"
log="$dir/hook.log"
now="$(date +%s)"

sid="$(printf '%s' "$input" | jq -r '.session_id // empty' 2>/dev/null)"
sid="${sid:-unknown}"
f="$dir/sessions/$sid"
prev="$(cut -d' ' -f1 "$f" 2>/dev/null)"

tool="$(printf '%s' "$input" | jq -r '.tool_name // empty' 2>/dev/null)"
case "$event" in
  prompt) state=working ;;
  posttool)
    # The Claude desktop app's wrap-up box with a question: after the call the session is still
    # waiting for an answer. Without this the question was immediately overwritten by
    # "working" and then "done".
    case "$tool" in
      *wrap_up) [ "$prev" = "question-pending" ] && state=question-pending || state=working ;;
      *) state=working ;;
    esac ;;
  tool)
    ask="$(printf '%s' "$input" | jq -r '.tool_input.ask // empty' 2>/dev/null)"
    case "$tool" in
      AskUserQuestion|ExitPlanMode) state=question ;;
      *wrap_up)
        # Raw input from the latest wrap-up box, for debugging (overwritten each time)
        printf '%s' "$input" | jq -c '{tool_name, ask: .tool_input.ask}' > "$dir/last-wrap_up.json" 2>/dev/null
        [ -n "$ask" ] && state=question-pending || state=working ;;
      *) state=working ;;
    esac ;;
  notify) state=question ;;
  # If the turn stops with an unanswered question in the box, it is still a question.
  stop) [ "$prev" = "question-pending" ] && state=question || state=done ;;
  end) state=gone ;;
  *) state=idle ;;
esac

# Write to a temp file and move it into place, so other sessions never read a half-written file.
if [ "$state" = gone ]; then rm -f "$f"; else echo "$state $now" > "$dir/.new.$$" && mv -f "$dir/.new.$$" "$f"; fi

# Combine all sessions. Limits in seconds for when a state counts as stale.
best=idle; rank=0; nw=0; nq=0; nv=0
for g in "$dir"/sessions/*; do
  [ -f "$g" ] || continue
  read -r s t < "$g" || continue
  age=$(( now - ${t:-0} ))
  case "$s" in
    question|question-pending) [ $age -lt 3600 ] && r=4 && s=question || r=0 ;;
    working) [ $age -lt 300 ] && r=3 || r=0 ;;
    done) [ $age -lt 20 ] && r=2 || r=0; [ $age -lt 900 ] && nv=$((nv + 1)) ;;
    *) r=0 ;;
  esac
  # Old files (over a day) from sessions that never said "done" are cleaned up.
  [ $age -gt 86400 ] && rm -f "$g"
  [ $r -eq 3 ] && nw=$((nw + 1))
  [ $r -eq 4 ] && nq=$((nq + 1))
  [ $r -gt $rank ] && rank=$r && best=$s
done
[ "$state" = question-pending ] && best=question

# Find the clock: last known address in $dir/ip, otherwise the fixed IP on the hotspot. If it
# doesn't answer, agentur.local (mDNS) is looked up again in the background and remembered.
host="$(cat "$dir/ip" 2>/dev/null)"
if [ -z "$host" ] && ip -4 -o addr 2>/dev/null | grep -q ' 10\.42\.0\.1/'; then
  host=10.42.0.2
fi
seq="$(date +%s%3N)"
url_args="s=${best}&w=${nw}&q=${nq}&v=${nv}&n=${seq}"

t0="$(date +%H:%M:%S.%3N)"
(
  send() { curl -s -m 2 -o /dev/null -w '%{http_code} %{time_total}' "http://$1/state?${url_args}"; }
  code=""
  [ -n "$host" ] && code="$(send "$host")"
  if [ "${code%% *}" != "200" ]; then
    # Unknown or outdated address: look it up again, one lookup at a time.
    exec 9>"$dir/lookup.lock"
    if flock -n 9; then
      new="$(timeout 4 getent ahostsv4 agentur.local | awk 'NR==1{print $1}')"
      if [ -n "$new" ]; then echo "$new" > "$dir/ip"; host="$new"; code="$(send "$host")"; fi
    fi
  fi
  echo "$t0 ${sid:0:8} ${event}/${state}${tool:+ ($tool)} -> ${best} w=${nw} q=${nq} v=${nv} @${host:-?} ${code:-none}" >> "$log"
  # Keep the log short
  [ "$(wc -l < "$log")" -gt 500 ] && tail -200 "$log" > "$log.tmp" && mv "$log.tmp" "$log"
) >/dev/null 2>&1 &
exit 0
