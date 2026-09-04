GHC = ghc
GHCFLAGS = -Wall -O0 -isrc

.PHONY: all build test test-unit test-integration run clean

all: build

build: bin/ircbot bin/mock-server

bin/ircbot: app/Main.hs src/Irc/Message.hs src/Irc/Bot.hs src/Irc/Commands.hs
	mkdir -p bin build/bot
	$(GHC) $(GHCFLAGS) -iapp -odir build/bot -hidir build/bot -o bin/ircbot app/Main.hs

bin/mock-server: test/mock/Server.hs
	mkdir -p bin build/mock
	$(GHC) $(GHCFLAGS) -odir build/mock -hidir build/mock -o bin/mock-server test/mock/Server.hs

bin/spec: test/Spec.hs src/Irc/Message.hs src/Irc/Bot.hs src/Irc/Commands.hs
	mkdir -p bin build/spec
	$(GHC) $(GHCFLAGS) -itest -odir build/spec -hidir build/spec -o bin/spec test/Spec.hs

test-unit: bin/spec
	./bin/spec

test-integration: bin/ircbot bin/mock-server
	bash test/run_integration.sh

test: test-unit test-integration

run: build
	bash test/run_integration.sh

clean:
	rm -rf build bin
