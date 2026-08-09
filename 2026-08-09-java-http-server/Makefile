JAVAC = javac
JAVA = java
BIN = bin

.PHONY: all build test run clean

all: build

build:
	@mkdir -p $(BIN)
	$(JAVAC) -d $(BIN) src/*.java

test:
	@mkdir -p $(BIN)
	$(JAVAC) -d $(BIN) src/*.java tests/*.java
	$(JAVA) -cp $(BIN) TestRunner

run: build
	$(JAVA) -cp $(BIN) HttpServer $(or $(PORT),8080) $(or $(ROOT),public)

clean:
	rm -rf $(BIN)
