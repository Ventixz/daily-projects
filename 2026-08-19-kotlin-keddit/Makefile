KOTLINC = kotlinc
JAVA = java
STDLIB = /usr/share/java/kotlin-stdlib.jar
OUT = out

.PHONY: build test run clean

build:
	@mkdir -p $(OUT)
	$(KOTLINC) src/*.kt -d $(OUT)

test:
	@mkdir -p $(OUT)
	$(KOTLINC) src/*.kt test/*.kt -d $(OUT)
	$(JAVA) -cp "$(OUT):$(STDLIB)" keddit.TestsKt

run: build
	$(JAVA) -cp "$(OUT):$(STDLIB)" keddit.MainKt

clean:
	rm -rf $(OUT)
