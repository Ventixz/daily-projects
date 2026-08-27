OCAMLOPT = ocamlopt
SRC = src/memory.ml src/cpu.ml
BUILD = build

.PHONY: all test clean run

all: $(BUILD)/gbcpu

$(BUILD)/gbcpu: $(SRC) src/main.ml | $(BUILD)
	$(OCAMLOPT) -I src -o $@ $(SRC) src/main.ml

$(BUILD)/test_cpu: $(SRC) tests/test_cpu.ml | $(BUILD)
	$(OCAMLOPT) -I src -o $@ $(SRC) tests/test_cpu.ml

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_cpu
	./$(BUILD)/test_cpu

roms/fib.gb: roms/assemble.ml | $(BUILD)
	$(OCAMLOPT) -o $(BUILD)/assemble roms/assemble.ml
	./$(BUILD)/assemble

run: all roms/fib.gb
	./$(BUILD)/gbcpu roms/fib.gb

clean:
	rm -rf $(BUILD)
	rm -f src/*.cmi src/*.cmx src/*.o tests/*.cmi tests/*.cmx tests/*.o roms/*.cmi roms/*.cmx roms/*.o roms/fib.gb
