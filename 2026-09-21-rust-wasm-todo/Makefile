.PHONY: build test run e2e clean

build:
	rustup target add wasm32-unknown-unknown
	cargo build --release --target wasm32-unknown-unknown
	wasm-bindgen --target web --out-dir www/pkg target/wasm32-unknown-unknown/release/rust_wasm_todo.wasm

test:
	cargo test

run: build
	npx http-server www -p 8080 -c-1

e2e: build
	npm install --silent
	npx http-server www -p 8877 -c-1 --silent >/tmp/rust-wasm-todo-e2e-server.log 2>&1 & \
	server_pid=$$!; \
	trap "kill $$server_pid 2>/dev/null" EXIT; \
	sleep 1; \
	node e2e/smoke.mjs

clean:
	cargo clean
	rm -rf www/pkg node_modules
