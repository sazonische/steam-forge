#!/usr/bin/env bash
# Usage: tools/ci/build-linux.sh [release|dev] [all|configure|build]
# CI runs configure and build as separate steps, so the vcpkg packages are cached even when the build fails.
set -euo pipefail

build_mode="${1:-release}"
build_step="${2:-all}"
case "${build_mode}" in
	release)
		preset_name="linux-release"
		;;
	dev)
		preset_name="linux-dev"
		;;
	*)
		echo "Unknown mode '${build_mode}'. Use release or dev." >&2
		exit 1
		;;
esac
case "${build_step}" in
	all | configure | build) ;;
	*)
		echo "Unknown step '${build_step}'. Use all, configure or build." >&2
		exit 1
		;;
esac

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export VCPKG_ROOT="${VCPKG_ROOT:-/opt/vcpkg}"

vcpkg_baseline="$(sed -n 's/.*"builtin-baseline": *"\([0-9a-f]*\)".*/\1/p' "${repository_root}/vcpkg.json")"
if [[ -z "${vcpkg_baseline}" ]]; then
	echo "builtin-baseline not found in vcpkg.json" >&2
	exit 1
fi

if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then
	git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
	git -C "${VCPKG_ROOT}" checkout --quiet "${vcpkg_baseline}"
	"${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
fi

current_vcpkg_revision="$(git -C "${VCPKG_ROOT}" rev-parse HEAD)"
if [[ "${current_vcpkg_revision}" != "${vcpkg_baseline}" ]]; then
	echo "vcpkg at ${current_vcpkg_revision}, manifest baseline is ${vcpkg_baseline}" >&2
	exit 1
fi

configure_arguments=(-DSTEAMFORGE_PUBLISH_COMPILE_COMMANDS=OFF)
if [[ -n "${STEAMFORGE_VERSION:-}" ]]; then
	configure_arguments+=("-DSTEAMFORGE_VERSION=${STEAMFORGE_VERSION}")
fi

cd "${repository_root}"
if [[ "${build_step}" != "build" ]]; then
	# Container builds share the source tree with the host; keep the host's clangd database intact.
	cmake --preset "${preset_name}" "${configure_arguments[@]}"
fi
if [[ "${build_step}" != "configure" ]]; then
	cmake --build --preset "${preset_name}"
fi
