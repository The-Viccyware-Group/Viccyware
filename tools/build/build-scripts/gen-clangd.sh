#!/bin/bash

export GIT_PROJ_ROOT="$(
  cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 \
  && git rev-parse --show-toplevel
)"

cd "${GIT_PROJ_ROOT}"
echo '#DO NOT MODIFY THIS FILE. IF YOU WANT DIFFERENT DIAGNOSTICS, MODIFY tools/build/build-tools/clangd-template, then rebuild' > .clangd
cat tools/build/build-scripts/clangd-template >> .clangd
if [[ "$(uname)" == "Darwin" ]]; then
  sed -i '' "s|HOME_REPLACE|$HOME|g" .clangd
else
  sed -i "s|HOME_REPLACE|$HOME|g" .clangd
fi
