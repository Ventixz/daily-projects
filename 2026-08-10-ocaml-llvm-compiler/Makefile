OCAMLOPT := ocamlopt
CORE := src/ast.ml src/lexer.ml src/parser.ml src/codegen.ml

.PHONY: all build test examples clean

all: build

build: kalc

kalc: $(CORE) src/main.ml
	$(OCAMLOPT) -I src $(CORE) src/main.ml -o kalc

runtime.o: runtime.c
	clang -c runtime.c -o runtime.o

run_tests: $(CORE) tests/test_kalc.ml
	$(OCAMLOPT) -I src $(CORE) tests/test_kalc.ml -o run_tests

test: run_tests runtime.o
	./run_tests

# Compile and run every example program, for a look at generated IR and
# output side by side with the source.
examples: kalc runtime.o
	@for f in examples/*.kal; do \
		base=$$(basename $$f .kal); \
		echo "== $$base =="; \
		./kalc $$f > examples/$$base.ll && \
		clang examples/$$base.ll runtime.o -o examples/$$base && \
		./examples/$$base; \
	done

clean:
	rm -f kalc run_tests runtime.o
	rm -f src/*.cmi src/*.cmx src/*.o tests/*.cmi tests/*.cmx tests/*.o
	rm -f examples/*.ll
	find examples -maxdepth 1 -type f ! -name '*.kal' -exec rm -f {} +
