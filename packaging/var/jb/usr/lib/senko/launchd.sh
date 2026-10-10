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
