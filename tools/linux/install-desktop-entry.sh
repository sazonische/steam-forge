#!/usr/bin/env bash
# Usage: tools/linux/install-desktop-entry.sh /path/to/SteamForge
# Installs the desktop entry and the application icon for the current user.
set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 /path/to/SteamForge" >&2
	exit 1
fi

executable_path="$(realpath "$1")"
if [[ ! -x "${executable_path}" ]]; then
	echo "${executable_path} is not an executable file" >&2
	exit 1
fi

# Desktop entry Exec quoting reserves these characters; refuse instead of escaping them.
if [[ "${executable_path}" == *[\"\`\$\\]* ]]; then
	echo "The executable path contains characters that cannot be used in a desktop entry: ${executable_path}" >&2
	exit 1
fi

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
data_home="${XDG_DATA_HOME:-${HOME}/.local/share}"
applications_directory="${data_home}/applications"
icon_directory="${data_home}/icons/hicolor/scalable/apps"

sed_safe_executable_path="$(printf '%s' "${executable_path}" | sed 's/[&|]/\\&/g')"

mkdir -p "${applications_directory}" "${icon_directory}"
install -m 644 "${repository_root}/resources/icons/app.svg" "${icon_directory}/steamforge.svg"
sed "s|^Exec=.*|Exec=\"${sed_safe_executable_path}\"|" "${repository_root}/resources/linux/steamforge.desktop" > "${applications_directory}/steamforge.desktop"
chmod 644 "${applications_directory}/steamforge.desktop"

if command -v update-desktop-database > /dev/null; then
	update-desktop-database "${applications_directory}" || true
fi
if command -v gtk-update-icon-cache > /dev/null; then
	gtk-update-icon-cache --quiet "${data_home}/icons/hicolor" || true
fi

echo "Installed ${applications_directory}/steamforge.desktop and ${icon_directory}/steamforge.svg"
