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
	scala -classpath $(OUT) chain.TestRunner

run: build
	scala -classpath $(OUT) chain.Main

clean:
	rm -rf $(OUT)
