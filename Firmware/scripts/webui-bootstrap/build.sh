#!/bin/sh
# Rebuild SD/bootstrap.min.css and SD/bootstrap.min.js. Usage: npm ci && npm run build (or ./build.sh <SD dir>)
set -e
OUT=${1:?usage: build.sh <SD dir>}
node icons.js
npx sass --style=compressed --no-source-map --quiet-deps --silence-deprecation=import,global-builtin,color-functions \
  --load-path=node_modules --load-path=src src/iotawatt-bootstrap.scss "$OUT/bootstrap.min.css"
npx esbuild src/iotawatt-bootstrap.js --bundle --minify --format=iife --target=es2017 \
  --alias:@popperjs/core=./src/popper-none.js --banner:js="/*! Bootstrap v$(node -p 'require("bootstrap/package.json").version') (IoTaWatt build: Collapse, Dropdown, Offcanvas) | Copyright 2011-2026 The Bootstrap Authors | MIT License */" --legal-comments=inline --outfile="$OUT/bootstrap.min.js" --log-level=warning
