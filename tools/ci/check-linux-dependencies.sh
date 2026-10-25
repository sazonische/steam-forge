#!/usr/bin/env bash
# Usage: tools/ci/check-linux-dependencies.sh <executable> [maximum-glibc-version]
set -euo pipefail

executable_path="$1"
maximum_glibc_version="${2:-2.28}"

# System libraries of every desktop distribution. libdbus stays shared: the vcpkg dbus port is ONLY_DYNAMIC_LIBRARY.
allowed_library_pattern='^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|ld-linux-x86-64\.so\.2|libX11\.so\.6|libX11-xcb\.so\.1|libXext\.so\.6|libXi\.so\.6|libXrender\.so\.1|libxcb[a-z0-9-]*\.so\.[0-9]+|libxkbcommon\.so\.0|libxkbcommon-x11\.so\.0|libdbus-1\.so\.3)$'

needed_libraries="$(readelf -d "${executable_path}" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p')"
echo "NEEDED entries of ${executable_path}:"
echo "${needed_libraries}"

unexpected_libraries=""
while IFS= read -r library_name; do
	if [[ -z "${library_name}" ]]; then
		continue
	fi
	if [[ ! "${library_name}" =~ ${allowed_library_pattern} ]]; then
		unexpected_libraries+="${library_name} "
	fi
done <<< "${needed_libraries}"

if [[ -n "${unexpected_libraries}" ]]; then
	echo "Unexpected shared libraries: ${unexpected_libraries}" >&2
	exit 1
fi

if objdump -T "${executable_path}" | grep -E -q 'GLIBCXX_|CXXABI_'; then
	echo "The executable depends on the shared libstdc++." >&2
	exit 1
fi

highest_glibc_version="$(objdump -T "${executable_path}" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -n 1)"
echo "Highest required glibc symbol version: ${highest_glibc_version}"
newest_of_both="$(printf '%s\n%s\n' "${highest_glibc_version}" "${maximum_glibc_version}" | sort -V | tail -n 1)"
if [[ "${newest_of_both}" != "${maximum_glibc_version}" ]]; then
	echo "The executable requires glibc ${highest_glibc_version}, newer than ${maximum_glibc_version}." >&2
	exit 1
fi

echo "Dependency check passed."
