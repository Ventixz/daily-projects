.PHONY: test build run clean

test:
	go test ./...

build:
	go build -o bin/bookstore .

run: build
	./bin/bookstore

clean:
	rm -rf bin
