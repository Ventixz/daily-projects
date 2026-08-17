SRC_DIR := src
TEST_DIR := test
OUT := out

SRC := $(wildcard $(SRC_DIR)/*.scala)
TEST_SRC := $(wildcard $(TEST_DIR)/*.scala)

.PHONY: build test run clean

build: $(OUT)/.stamp

$(OUT)/.stamp: $(SRC)
	mkdir -p $(OUT)
	scalac -d $(OUT) $(SRC)
	touch $(OUT)/.stamp

test: build
	scalac -d $(OUT) -classpath $(OUT) $(TEST_SRC)
	scala -classpath $(OUT) regex.TestRunner

run: build
	scala -classpath $(OUT) regex.Main $(ARGS)

clean:
	rm -rf $(OUT)
