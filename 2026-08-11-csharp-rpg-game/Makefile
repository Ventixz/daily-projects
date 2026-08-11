DOTNET = dotnet

.PHONY: all build test run clean

all: build

build:
	$(DOTNET) build src/RpgGame.csproj

test:
	$(DOTNET) run --project tests/RpgGame.Tests.csproj

run: build
	$(DOTNET) run --project src/RpgGame.csproj

clean:
	rm -rf src/bin src/obj tests/bin tests/obj
