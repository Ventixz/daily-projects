RSCRIPT = Rscript

.PHONY: test run

test:
	$(RSCRIPT) test/test_apriori.R

run:
	$(RSCRIPT) src/main.R resources/transactions.txt 0.6 0.75
