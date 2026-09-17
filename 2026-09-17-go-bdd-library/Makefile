.PHONY: test unit bdd build run clean

test: unit bdd

unit:
	go test ./internal/...

bdd:
	go test ./test/bdd/... -v

build:
	go build -o bin/library-demo .

run: build
	./bin/library-demo

clean:
	rm -rf bin
