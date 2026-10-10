# ios 5 and 6 installers can inherit mobile's bootstrap and a stale launchd socket
LAUNCHCTL_CONTEXT=direct

launchctl_cmd() {
  (
    unset LAUNCHD_SOCKET LaunchKeepContext
    case "$LAUNCHCTL_CONTEXT" in
      system) "$LAUNCHCTL" -1 "$@" ;;
      bsexec) "$LAUNCHCTL" bsexec 1 "$LAUNCHCTL" "$@" ;;
      direct) "$LAUNCHCTL" "$@" ;;
      *) return 1 ;;
    esac
  )
}

launchctl_system_context() {
  # modern launchctl names the domain explicitly; older releases expose managername
  domain="$(launchctl_cmd print system 2>/dev/null)"
  if [ $? = 0 ]; then
    case "$domain" in
      'system = {'*) launchctl_cmd list >/dev/null 2>&1; return $? ;;
    esac
  fi
  manager="$(launchctl_cmd managername 2>/dev/null)" || return 1
  [ "$manager" = System ] || return 1
  launchctl_cmd list >/dev/null 2>&1
}

select_launchctl_context() {
  for LAUNCHCTL_CONTEXT in direct system bsexec; do
    launchctl_system_context && return 0
  done
  LAUNCHCTL_CONTEXT=direct
  return 1
}

# a matching substring can be another job, and a failed list is not an empty domain
launchd_loaded() {
  jobs="$(launchctl_cmd list 2>/dev/null)" || return 2
  printf '%s\n' "$jobs" | (
    while read -r pid status job extra; do
      [ "$job" = com.senko.senkod ] && [ -z "$extra" ] && exit 0
    done
    exit 1
  )
}

# ios 6 launchctl lists jobs over mach but loads and unloads them through
# /var/tmp/launchd/sock. an iphone 4s on 6.1.3 loses that file when /var/tmp is
# emptied after boot, and every load and unload fails until the next boot while
# jobs loaded at boot keep running under KeepAlive
launchd_socket_lost() {
  case "$(launchctl_cmd list com.senko.senkod 2>&1)" in
    *'launch_msg(): Socket is not connected'*) return 0 ;;
  esac
  return 1
}

# prints "-" while launchd holds the job without a running process
launchd_job_pid() {
  launchctl_cmd list 2>/dev/null | (
    while read -r pid status job extra; do
      [ "$job" = com.senko.senkod ] && [ -z "$extra" ] && { echo "$pid"; exit 0; }
    done
    exit 1
  )
}

# h3lix on ios 10 accepts bootout without unloading the job
launchd_stop() {
  [ -x "$LAUNCHCTL" ] && select_launchctl_context || {
    echo "senko: cannot reach system launchd to stop senkod" >&2
    return 1
  }
  for step in check bootout_label bootout_plist unload remove; do
    case "$step" in
      bootout_label) launchctl_cmd bootout system/com.senko.senkod >/dev/null 2>&1 ;;
      bootout_plist) launchctl_cmd bootout system "$PLIST" >/dev/null 2>&1 ;;
      unload) launchctl_cmd unload "$PLIST" >/dev/null 2>&1 ;;
      remove) launchctl_cmd remove com.senko.senkod >/dev/null 2>&1 ;;
    esac
    launchd_loaded
    case $? in
      1) return 0 ;;
      2) echo "senko: cannot verify senkod was unloaded" >&2; return 1 ;;
    esac
  done
  echo "senko: launchd still holds com.senko.senkod" >&2
  return 1
}
