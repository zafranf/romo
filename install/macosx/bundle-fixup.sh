#!/usr/bin/env bash
# bundle-fixup.sh — make Romo.app self-contained.
#
# Copies every non-system dylib dependency referenced by any Mach-O inside
# the bundle into Contents/Frameworks and rewrites the load commands to
# @executable_path/../Frameworks, so the app no longer depends on the
# Homebrew Qt it was built against. Finally re-signs everything ad-hoc
# (required on Apple Silicon) and fails if any absolute non-system
# dependency remains.
#
# Usage: bundle-fixup.sh /path/to/Romo.app

set -euo pipefail

APP="${1:?usage: bundle-fixup.sh <Romo.app>}"
FW="$APP/Contents/Frameworks"

[ -d "$APP" ] || { echo "bundle-fixup: no such bundle: $APP" >&2; exit 1; }
mkdir -p "$FW"

# Stale artifacts from earlier install schemes: Qt .framework copies (the
# app now links flat dylibs from Frameworks/) and the Frameworks/lib subdir.
rm -rf "$FW"/Qt*.framework "$FW/lib"

is_macho() {
    file -b "$1" 2>/dev/null | grep -q 'Mach-O'
}

# All Mach-O files in the bundle: main binary, plugins, dylibs.
macho_files() {
    find "$APP/Contents" \( -type f -o -type l \) | while IFS= read -r f; do
        is_macho "$f" && printf '%s\n' "$f"
    done
}

is_system() {
    case "$1" in
        /usr/lib/*|/System/*|@executable_path/*|@loader_path/*|@rpath/*) return 0 ;;
        *) return 1 ;;
    esac
}

# Dependency list of one Mach-O, excluding its own LC_ID_DYLIB.
deps_of() {
    local f="$1" id
    id="$(otool -D "$f" 2>/dev/null | sed -n 2p || true)"
    otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | while IFS= read -r dep; do
        [ "$dep" = "$id" ] && continue
        is_system "$dep" && continue
        printf '%s\n' "$dep"
    done
}

# Passes: copy missing deps, rewrite load commands, until closure is stable.
pass=0
while :; do
    pass=$((pass + 1))
    if [ "$pass" -gt 8 ]; then
        echo "bundle-fixup: dependency closure did not converge after 8 passes" >&2
        exit 1
    fi
    todo=0
    while IFS= read -r f; do
        while IFS= read -r dep; do
            [ -n "$dep" ] || continue
            base="$(basename "$dep")"
            target="$FW/$base"
            if [ ! -f "$target" ]; then
                # If the source is gone from this machine, a pre-seeded copy
                # may already sit in the bundle (e.g. OpenSSL) — then just
                # rewrite below. Otherwise this is a real error.
                [ -f "$dep" ] || {
                    echo "bundle-fixup: missing dependency source: $dep (needed by $f)" >&2
                    exit 1
                }
                cp -f "$dep" "$target"
                chmod u+w "$target"
                strip -S "$target" 2>/dev/null || true
                install_name_tool -id "@rpath/$base" "$target" 2>/dev/null || true
                echo "  + $base"
            fi
            install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" "$f"
            todo=1
        done < <(deps_of "$f")
    done < <(macho_files)
    [ "$todo" -eq 0 ] && break
done

# Verify: nothing left that would make the app depend on the build machine.
fail=0
while IFS= read -r f; do
    while IFS= read -r dep; do
        echo "bundle-fixup: UNRESOLVED $dep (in $f)" >&2
        fail=1
    done < <(deps_of "$f")
done < <(macho_files)
[ "$fail" -eq 0 ] || exit 1

# Rewrite LC_ID_DYLIB of bundled dylibs to @rpath/<name> (cosmetic: dyld
# resolves load commands, not IDs, but this keeps audits trivially clean).
while IFS= read -r f; do
    case "$f" in
        *.dylib|*/Frameworks/Qt*) ;;
        *) continue ;;
    esac
    id="$(otool -D "$f" 2>/dev/null | sed -n 2p || true)"
    [ -n "$id" ] || continue
    case "$id" in
        @*|/usr/lib/*|/System/*) continue ;;
    esac
    install_name_tool -id "@rpath/$(basename "$f")" "$f" 2>/dev/null || true
done < <(macho_files)

# Re-sign everything ad-hoc: Apple Silicon rejects code with broken
# signatures, and every file we rewrote lost its original one.
while IFS= read -r f; do
    codesign -f -s - "$f" > /dev/null
done < <(macho_files)
codesign -f -s - "$APP" > /dev/null

echo "bundle-fixup: OK — $(basename "$APP") is self-contained"
