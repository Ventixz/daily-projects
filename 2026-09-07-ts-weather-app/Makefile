# The e2e harness needs `playwright`, which this project does not install
# itself (see package.json) -- it borrows the globally-installed copy via
# NODE_PATH. Point that at wherever your global node_modules lives if it's
# not the default below.
GLOBAL_NODE_MODULES ?= /opt/node22/lib/node_modules

.PHONY: build test unit e2e serve clean

build:
	npm install --silent
	npx tsc

test: unit e2e

unit: build
	node dist/tests/unit/run.js

e2e: build
	NODE_PATH=$(GLOBAL_NODE_MODULES) node dist/tests/e2e/run.js

serve: build
	node server.js

clean:
	rm -rf dist node_modules
