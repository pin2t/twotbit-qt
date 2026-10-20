#!/bin/sh
# Adds the Twotbit folder this script sits in to your applications: its
# desktop entry, pointed at this folder's twotbit, and its icons, in
# ~/.local/share. The folder stays where it is, so move it first, or run the
# script again after moving it. polkit reads the action of the password
# check before the recovery phrase is shown only from
# /usr/share/polkit-1/actions, so it is copied there with sudo.
#
#     ./install.sh

set -eu

here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
id=com.pin2t.Twotbit

# The command quoted as desktop entries quote it: the characters the quotes
# do not cover take two backslashes, and % is doubled.
command=$(printf '%s' "$here/bin/twotbit" | sed -e 's/[\\"`$]/\\\\&/g' -e 's/%/%%/g')
mkdir -p "$data/applications"
{
    grep -v '^Exec=' "$here/share/applications/$id.desktop"
    printf 'Exec="%s"\n' "$command"
} >"$data/applications/$id.desktop"

for icon in "$here"/share/icons/hicolor/*/apps/$id.png; do
    size=$(basename "$(dirname "$(dirname "$icon")")")
    mkdir -p "$data/icons/hicolor/$size/apps"
    cp "$icon" "$data/icons/hicolor/$size/apps/$id.png"
done
if command -v update-desktop-database >/dev/null; then update-desktop-database -q "$data/applications" || true; fi
if command -v gtk-update-icon-cache >/dev/null; then gtk-update-icon-cache -q -t "$data/icons/hicolor" || true; fi

policy=com.pin2t.twotbit.policy
actions=/usr/share/polkit-1/actions
if [ -d "$actions" ] && ! cmp -s "$here/share/polkit-1/actions/$policy" "$actions/$policy"; then
    echo "Installing the polkit action that asks for your password before the recovery phrase is shown:"
    if ! sudo install -m 644 "$here/share/polkit-1/actions/$policy" "$actions/$policy"; then
        echo "Not installed: Settings will show the recovery phrase without asking for your password." >&2
    fi
fi

echo "Twotbit is in your applications. It runs $here/bin/twotbit."
