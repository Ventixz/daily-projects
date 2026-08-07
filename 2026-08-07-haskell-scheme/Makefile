GHC := ghc
GHCFLAGS := -isrc -Wall
OBJDIR := bin/obj

.PHONY: build test repl run clean

build: bin/scheme

bin/scheme: src/*.hs
	mkdir -p bin
	$(GHC) $(GHCFLAGS) -outputdir $(OBJDIR) -o bin/scheme src/Main.hs

bin/spec: src/*.hs tests/Spec.hs
	mkdir -p bin
	$(GHC) $(GHCFLAGS) -itests -outputdir $(OBJDIR) -o bin/spec tests/Spec.hs

test: bin/spec
	./bin/spec

repl: build
	./bin/scheme

# Usage: make run FILE=examples/fib.scm
run: build
	./bin/scheme $(FILE)

clean:
	rm -rf bin
