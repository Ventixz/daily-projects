.PHONY: test demo build clean

test:
	go test ./...

build:
	go build -o bin/gotorrent ./cmd/gotorrent
	go build -o bin/demoseeder ./cmd/demoseeder
	go build -o bin/gentorrent ./cmd/gentorrent

demo: build
	bash scripts/demo.sh

clean:
	rm -rf bin resources/demo.torrent
