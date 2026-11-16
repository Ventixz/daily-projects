ERLC := erlc
ERL  := erl
EBIN := ebin
PORT ?= 8080

.PHONY: all compile test run clean

all: compile

compile:
	mkdir -p $(EBIN)
	$(ERLC) -Werror -o $(EBIN) src/*.erl
	cp src/wschat.app.src $(EBIN)/wschat.app

EUNIT_EBIN := $(shell $(ERL) -noshell -eval 'io:format("~s", [code:lib_dir(eunit, ebin)]), halt().')

test: compile
	$(ERLC) -Werror -pa $(EUNIT_EBIN) -pa $(EBIN) -o $(EBIN) test/*.erl
	$(ERL) -noshell -pa $(EBIN) -s run_tests main

# Open http://localhost:8080 in two browser tabs. Add ?transport=poll to
# the URL in one of them to force the long-poll fallback.
run: compile
	$(ERL) -noshell -pa $(EBIN) -eval 'application:load(wschat), application:set_env(wschat, port, $(PORT)), {ok, _} = application:ensure_all_started(wschat), io:format("wschat listening on http://localhost:~p~n", [wschat_listener:port()]).'

clean:
	rm -rf $(EBIN)
