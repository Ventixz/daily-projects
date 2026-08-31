.PHONY: test race run build clean

test:
	go test ./...

race:
	go test -race ./...

run:
	go run .

build:
	go build -o bin/gorest .

clean:
	rm -rf bin
