JAVAC = javac
JAVA = java
BIN = bin

.PHONY: all build test run repl clean

all: build

build:
	@mkdir -p $(BIN)
	$(JAVAC) -d $(BIN) src/*.java

test:
	@mkdir -p $(BIN)
	$(JAVAC) -d $(BIN) src/*.java tests/*.java
	$(JAVA) -cp $(BIN) TestRunner

run: build
	$(JAVA) -cp $(BIN) Lox $(SCRIPT)

repl: build
	$(JAVA) -cp $(BIN) Lox

clean:
	rm -rf $(BIN)
