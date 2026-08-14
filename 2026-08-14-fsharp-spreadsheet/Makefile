DOTNET = dotnet

.PHONY: all build test run repl clean

all: build

build:
	$(DOTNET) build src/Spreadsheet.Core.fsproj
	$(DOTNET) build cli/Spreadsheet.Cli.fsproj

test:
	$(DOTNET) run --project tests/Spreadsheet.Tests.fsproj

run: build
	$(DOTNET) run --project cli/Spreadsheet.Cli.fsproj -- examples/budget.sheet

repl: build
	$(DOTNET) run --project cli/Spreadsheet.Cli.fsproj -- --repl

clean:
	rm -rf src/bin src/obj cli/bin cli/obj tests/bin tests/obj
