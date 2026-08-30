.PHONY: build test run clean

build:
	npm install --silent
	npx tsc

test: build
	node --test dist/**/*.test.js

run: build
	node dist/main.js

clean:
	rm -rf dist node_modules
