.PHONY: test race run build clean

test:
	go test ./...

race:
	go test -race ./...

build:
	go build -o bin/blockchain .

run: build
	./bin/blockchain printchain

clean:
	rm -rf bin blockchain.db
