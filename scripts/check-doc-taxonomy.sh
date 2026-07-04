#!/usr/bin/env bash
set -euo pipefail

fail=0

error() {
  printf 'doc-taxonomy: %s\n' "$1" >&2
  fail=1
}

is_moved_pointer() {
  local path="$1"
  grep -Eq '^Status: moved\.$|^Status: moved$' "$path"
}

# Local scratch files must remain untracked. This catches committed files if the
# ignore rule is bypassed.
while IFS= read -r path; do
  case "$path" in
    *.local.md|*.local.*)
      error "local scratch file is tracked: $path"
      ;;
  esac
done < <(git ls-files)

# SPEC is normative except explicit moved compatibility pointers.
while IFS= read -r path; do
  base="${path##*/}"
  if is_moved_pointer "$path"; then
    continue
  fi
  case "$base" in
    *.feature-*.md|*.rc[0-9]*.md|*.milestone-*.md|*.note.md|*.experiment.md|*.archive.md)
      error "non-normative lifecycle file under SPEC: $path"
      ;;
  esac
  if grep -Eiq 'Status: (non-normative|active .*checklist|completed execution plan)|^# Task:|^# .*Checklist$|^# .*Readiness$|Proposal' "$path"; then
    case "$base" in
      README.md) ;;
      *) error "SPEC file contains planning/design wording: $path" ;;
    esac
  fi
done < <(git ls-files 'SPEC/*.md')

# Docs is stable usage documentation, not active work tracking. Moved pointers
# are allowed for compatibility.
while IFS= read -r path; do
  base="${path##*/}"
  if is_moved_pointer "$path"; then
    continue
  fi
  case "$base" in
    *.feature-*.md|*.rc[0-9]*.md|*.milestone-*.md|*.experiment.md|*.archive.md)
      error "lifecycle planning/design file under Docs: $path"
      ;;
  esac
  if grep -Eiq 'Status: active .*checklist|completed execution plan|^# Task:|^# .*Tasks$|^# .*Checklist$|^# .*Readiness$' "$path"; then
    error "active planning/checklist content under Docs: $path"
  fi
done < <(git ls-files 'Docs/*.md')

# Design files should clearly identify that they are non-normative/design docs
# unless they are the directory README.
while IFS= read -r path; do
  base="${path##*/}"
  [ "$base" = "README.md" ] && continue
  if ! grep -Eiq 'non-normative|decision|proposal|design|direction' "$path"; then
    error "design file lacks non-normative/design status wording: $path"
  fi
done < <(git ls-files 'Design/*.md')

# Planning files should identify status/scope/gate/readiness context.
while IFS= read -r path; do
  base="${path##*/}"
  [ "$base" = "README.md" ] && continue
  if ! grep -Eiq 'Status:|Scope|Gate|Milestone|Readiness|Exit|Objective|Goal|Checklist' "$path"; then
    error "planning file lacks status/scope/gate wording: $path"
  fi
done < <(git ls-files 'Planning/*.md')

exit "$fail"
