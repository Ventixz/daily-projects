# No Clojure CLI or Leiningen needed: Java 21 + three jars from Maven Central.
# On Windows without make, use the Clojure CLI with deps.edn instead (see LEARNING.md).

MAVEN = https://repo1.maven.org/maven2/org/clojure
JARS  = lib/clojure-1.12.0.jar lib/spec.alpha-0.5.238.jar lib/core.specs.alpha-0.4.74.jar
CP    = src:resources:$(subst $() $(),:,$(JARS))

.PHONY: deps test run generate clean

deps: $(JARS)

lib/clojure-1.12.0.jar:          SHA1 = 8e0b7e60ea3c8f1ef4273d695b27a6cb85000571
lib/clojure-1.12.0.jar:          URL  = $(MAVEN)/clojure/1.12.0/clojure-1.12.0.jar
lib/spec.alpha-0.5.238.jar:      SHA1 = 4eb5dea521c4e6e1f68c2c47517f14a922003e60
lib/spec.alpha-0.5.238.jar:      URL  = $(MAVEN)/spec.alpha/0.5.238/spec.alpha-0.5.238.jar
lib/core.specs.alpha-0.4.74.jar: SHA1 = d56a8d4c666ff8140e6d0a62d41263134be39254
lib/core.specs.alpha-0.4.74.jar: URL  = $(MAVEN)/core.specs.alpha/0.4.74/core.specs.alpha-0.4.74.jar

# Download, check the SHA-1 Maven Central publishes, then move into place.
$(JARS):
	@mkdir -p lib
	curl -sSfL -o $@.part $(URL)
	echo "$(SHA1)  $@.part" | sha1sum -c --quiet -
	mv $@.part $@

test: deps
	java -cp "test:$(CP)" clojure.main -m twitter-bot.test-runner

generate: deps
	java -cp "$(CP)" clojure.main -m twitter-bot.core generate $(ARGS)

run: deps
	java -cp "$(CP)" clojure.main -m twitter-bot.core $(ARGS)

clean:
	rm -rf lib
