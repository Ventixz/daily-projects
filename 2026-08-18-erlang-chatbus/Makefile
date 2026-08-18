ERLC := erlc
ERL  := erl
SRC  := src
TEST := test
EBIN := ebin

.PHONY: all compile test run shell clean

all: compile

compile:
	mkdir -p $(EBIN)
	$(ERLC) -Werror -o $(EBIN) $(SRC)/*.erl
	cp $(SRC)/chatbus.app.src $(EBIN)/chatbus.app

EUNIT_EBIN := $(shell erl -noshell -eval 'io:format("~s", [code:lib_dir(eunit, ebin)]), halt().')

test: compile
	$(ERLC) -Werror -pa $(EUNIT_EBIN) -o $(EBIN) $(TEST)/*.erl
	$(ERL) -noshell -pa $(EBIN) -s run_tests main

# Starts the OTP application in an interactive shell, listening on the
# configured port (5555 by default). Connect with: nc localhost 5555
run: compile
	$(ERL) -pa $(EBIN) -eval 'application:ensure_all_started(chatbus).'

shell: run

clean:
	rm -rf $(EBIN)
