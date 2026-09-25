GHC = ghc
GHCFLAGS = -Wall -isrc

.PHONY: all build test run repl clean

all: build

build: bin/poly

bin/poly: app/Main.hs src/*.hs
	mkdir -p bin build/obj
	$(GHC) $(GHCFLAGS) -O1 -o bin/poly app/Main.hs -outputdir build/obj

bin/test-runner: tests/Spec.hs src/*.hs
	mkdir -p bin build/obj-test
	$(GHC) $(GHCFLAGS) -O1 -o bin/test-runner tests/Spec.hs -outputdir build/obj-test

test: bin/test-runner
	./bin/test-runner

run: build
	./bin/poly examples/factorial.poly

repl: build
	./bin/poly

clean:
	rm -rf bin build
