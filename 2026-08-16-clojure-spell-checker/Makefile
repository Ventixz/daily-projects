CLOJURE_JAR = /usr/share/java/clojure-1.11.jar
CP = src:resources
TEST_CP = $(CP):test

.PHONY: test run clean

test:
	java -cp "$(TEST_CP):$(CLOJURE_JAR)" clojure.main -m spell-checker.test-runner

run:
	java -cp "$(CP):$(CLOJURE_JAR)" clojure.main -m spell-checker.core $(ARGS)

clean:
	find . -name "*.class" -delete
