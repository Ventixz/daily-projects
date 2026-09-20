.PHONY: build test run e2e clean

build:
	lein run

test:
	lein test

run: build
	npx http-server resources/public -p 8080 -c-1

e2e: build
	npm install --silent
	npx http-server resources/public -p 8934 -c-1 --silent >/tmp/tetris-e2e-server.log 2>&1 & \
	server_pid=$$!; \
	trap "kill $$server_pid 2>/dev/null" EXIT; \
	sleep 1; \
	node e2e/smoke.mjs

clean:
	rm -rf target .cpcache resources/public/js/main.js node_modules
