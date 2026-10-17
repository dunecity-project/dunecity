#!/bin/bash
# List DuneCity worktrees whose commit is already merged into the integration
# branch or origin/main, with the size of their configured build trees.
# A worktree is a candidate when it is merged, clean (no uncommitted or
# untracked files) and idle (no process has a file or cwd below it or names it
# on its command line).
#
#   scripts/prune-merged-worktrees.sh                    # dry run (default)
#   scripts/prune-merged-worktrees.sh --apply            # delete the candidates' build*/
#   scripts/prune-merged-worktrees.sh --remove-worktree  # ...and `git worktree remove` them
#   --base REF (repeatable) replaces the default bases; --keep PATH protects a worktree.
#
# Never touches the main worktree, the staging checkout or the worktree it runs
# from. Only directories holding a CMakeCache.txt count as build trees. Branches
# are kept: `git worktree remove` deletes only the checkout.
set -euo pipefail

apply=0
remove_worktree=0
bases=()
keep=()
while [ $# -gt 0 ]; do
    case "$1" in
        --apply) apply=1 ;;
        --remove-worktree) apply=1; remove_worktree=1 ;;
        --base) bases+=("$2"); shift ;;
        --keep) keep+=("$(cd "$2" && pwd -P)"); shift ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

here=$(git rev-parse --show-toplevel)
here=$(cd "$here" && pwd -P)
[ ${#bases[@]} -gt 0 ] || bases=(integrate/feature-batch origin/main)
valid_bases=()
for base in "${bases[@]}"; do
    git rev-parse -q --verify "$base^{commit}" >/dev/null && valid_bases+=("$base")
done
[ ${#valid_bases[@]} -gt 0 ] || { echo "no merge base ref exists: ${bases[*]}" >&2; exit 2; }
staging=$(git config --get dunecity.stagingCheckout || echo "$HOME/Documents/projects/dunecity-feature-batch")
staging=$(cd "$staging" 2>/dev/null && pwd -P || echo "$staging")

open_files=$(lsof -nP -w -Fn 2>/dev/null | sed -n 's/^n\(\/.*\)/\1/p' || true)
commands=$(ps -axww -o args= 2>/dev/null | grep -v prune-merged-worktrees || true)

in_use() {
    # $1: absolute path; open files/cwd below it, or a command line naming it
    # (followed by /, whitespace, a quote or the end: not a longer sibling name).
    printf '%s\n' "$open_files" | grep -qF "$1/" && return 0
    printf '%s\n' "$open_files" | grep -qxF "$1" && return 0
    local pattern
    pattern=$(printf '%s' "$1" | sed 's/[][\.*^$()+?{}|]/\\&/g')
    printf '%s\n' "$commands" | grep -qE -- "$pattern"'($|[/[:space:]"'"'"'])' && return 0
    return 1
}

total_kb=0
freed_kb=0
first=1
gib() { awk -v kb="$1" 'BEGIN { printf "%.1f", kb / 1048576 }'; }
printf '%-46s %-36s %-26s %-5s %-5s %-9s %s\n' WORKTREE BRANCH MERGED-INTO LIVE CLEAN CANDIDATE BUILD-TREES
while IFS= read -r line; do
    case "$line" in
        "worktree "*) path=${line#worktree }; head=""; branch="(detached)" ;;
        "HEAD "*) head=${line#HEAD } ;;
        "branch "*) branch=${line#branch refs/heads/} ;;
        "")
            [ -n "${path:-}" ] || continue
            this=$path; path=""
            if [ "$first" = 1 ]; then first=0; continue; fi   # main worktree holds the repository
            [ -d "$this" ] || continue
            real=$(cd "$this" && pwd -P)
            skip=""
            [ "$real" = "$here" ] && skip="current"
            [ "$real" = "$staging" ] && skip="staging"
            for k in "${keep[@]+"${keep[@]}"}"; do [ "$real" = "$k" ] && skip="kept"; done
            merged=""
            for base in "${valid_bases[@]}"; do
                if git merge-base --is-ancestor "$head" "$base" 2>/dev/null; then merged=$base; break; fi
            done
            live=no; in_use "$real" && live=yes
            clean=no
            # An untracked build*/ tree (only /build is ignored) is what this script removes;
            # a worktree git cannot read never counts as clean.
            status=$(git -C "$real" status --porcelain --untracked-files=normal 2>/dev/null) || status="unreadable"
            [ -z "$(printf '%s\n' "$status" | grep -vE '^(\?\? build[^/]*/)?$' || true)" ] && clean=yes
            builds=""; wt_kb=0
            for dir in "$real"/build*; do
                [ -d "$dir" ] && [ ! -L "$dir" ] && [ -f "$dir/CMakeCache.txt" ] || continue
                kb=$(du -sk "$dir" 2>/dev/null | cut -f1)
                wt_kb=$((wt_kb + kb)); builds="$builds $(basename "$dir")=$(gib "$kb")G"
            done
            candidate=no
            [ -n "$merged" ] && [ -z "$skip" ] && [ "$live" = no ] && [ "$clean" = yes ] && candidate=yes
            printf '%-46s %-36s %-26s %-5s %-5s %-9s %s%s\n' "${real/#$HOME/~}" "$branch" "${merged:--}" "$live" "$clean" \
                "$candidate" "${builds:- -}" "${skip:+ [skip: $skip]}"
            [ "$candidate" = yes ] || continue
            total_kb=$((total_kb + wt_kb))
            [ "$apply" = 1 ] || continue
            for dir in "$real"/build*; do
                [ -d "$dir" ] && [ ! -L "$dir" ] && [ -f "$dir/CMakeCache.txt" ] || continue
                rm -rf -- "$dir" && echo "  removed $dir"
            done
            freed_kb=$((freed_kb + wt_kb))
            if [ "$remove_worktree" = 1 ]; then
                git worktree remove "$real" && echo "  removed worktree $real (branch $branch kept)"
            fi
            ;;
    esac
done < <(git worktree list --porcelain; echo)

if [ "$apply" = 1 ]; then
    echo "freed $(gib "$freed_kb") GiB of build trees"
else
    echo "dry run: $(gib "$total_kb") GiB of build trees in merged, clean, idle worktrees (re-run with --apply)"
fi
