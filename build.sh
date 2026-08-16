#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Build the development container and run GMAN's gates inside it. This is the
# same toolchain CI uses, so a green run here is a good predictor of a green
# push -- unlike a macOS build, which cannot see gcc/libstdc++ or
# LeakSanitizer diagnostics.

set -euo pipefail

VERSION=$(git rev-parse HEAD | cut -c 1-10)
PROJECT=jac18281828/gmandev

docker build . --progress=plain -t "${PROJECT}:${VERSION}" --build-arg VERSION="${VERSION}"

# Mount the working tree rather than baking it in, so an edit on the host is
# visible without a rebuild. Build directories are container-local: object
# files from this toolchain must not collide with a host build.
docker run --rm -i -t \
	-v "$(pwd)":/workspaces/gman \
	-w /workspaces/gman \
	"${PROJECT}:${VERSION}" \
	bash -c '
		set -euo pipefail
		cmake -S . -B /tmp/build-dev -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror"
		cmake --build /tmp/build-dev -j"$(nproc)"
		ctest --test-dir /tmp/build-dev --output-on-failure
		yamlfmt -lint .github/workflows/*.yml
	'
