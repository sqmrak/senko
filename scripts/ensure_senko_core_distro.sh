#!/usr/bin/env bash
set -euo pipefail

SRC="${1:?usage: ensure_senko_core_distro.sh source-path}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH="${ROOT}/scripts/patches/senko-core-trim-distro.patch"
CONF_PATCH="${ROOT}/scripts/patches/senko-core-trim-conf.patch"
DISTRO="${SRC}/main/distro/senko/senko.go"

same_distro_code() {
  [ -f "${DISTRO}" ] || return 1
  grep -q '"github.com/xtls/xray-core/main/distro/senko"' "${SRC}/main/main.go" || return 1
  # an older checkout may carry the same imports with the old module name in
  # comments; compare executable lines so a renamed comment needs no source edit
  cmp -s \
    <(awk '
      /^diff --git a\/main\/distro\/senko\/senko.go/ { inside=1; next }
      /^diff --git / { inside=0 }
      inside && /^\+[^+]/ {
        sub(/^\+/, "")
        if ($0 !~ /^[[:space:]]*(\/\/|$)/) print
      }' "${PATCH}") \
    <(awk '$0 !~ /^[[:space:]]*(\/\/|$)/ { print }' "${DISTRO}")
}

conf_applied() {
  git -C "${SRC}" apply --reverse --check "${CONF_PATCH}" 2>/dev/null
}

# the paths the two maintained patches own, as git names them
owned_paths() {
  awk '/^diff --git a\// { sub(/^b\//, "", $4); print $4 }' "${PATCH}" "${CONF_PATCH}" | sort -u
}

# a source tree carrying an older version of these patches takes neither the
# current ones nor their reverse. it goes back to its pinned commit only when
# every change in it lies in a path the patches own, so no other edit is lost
reset_older_patch_set() {
  local owned path
  owned="$(owned_paths)"
  while IFS= read -r path; do
    [ -n "${path}" ] || continue
    if ! grep -qxF -- "${path}" <<<"${owned}"; then
      echo "senko-core source has a change outside the maintained patches: ${path}" >&2
      return 1
    fi
  done < <(git -C "${SRC}" status --porcelain --untracked-files=all | cut -c4-)
  echo "senko-core: replacing an older senko patch set in ${SRC}" >&2
  while IFS= read -r path; do
    if git -C "${SRC}" cat-file -e "HEAD:${path}" 2>/dev/null; then
      git -C "${SRC}" checkout -q HEAD -- "${path}"
    else
      rm -f "${SRC}/${path}"
    fi
  done <<<"${owned}"
}

if ! { same_distro_code || git -C "${SRC}" apply --check "${PATCH}" 2>/dev/null; } ||
   ! { conf_applied || git -C "${SRC}" apply --check "${CONF_PATCH}" 2>/dev/null; }; then
  reset_older_patch_set || exit 1
fi

if ! same_distro_code; then
  if git -C "${SRC}" apply --check "${PATCH}"; then
    git -C "${SRC}" apply "${PATCH}"
    same_distro_code || { echo "senko-core distro differs from the maintained patch" >&2; exit 1; }
  else
    echo "senko-core distro is missing or differs from the maintained patch" >&2
    exit 1
  fi
fi

# the senko_trim build tag only means something once this patch is in place
if ! conf_applied; then
  if git -C "${SRC}" apply --check "${CONF_PATCH}"; then
    git -C "${SRC}" apply "${CONF_PATCH}"
  else
    echo "senko-core infra/conf differs from the maintained trim patch" >&2
    exit 1
  fi
fi
