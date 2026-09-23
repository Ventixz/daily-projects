.PHONY: setup run test e2e clean

setup:
	pip install -r requirements.txt pytest requests

run:
	python3 microblog.py

test:
	python3 -m pytest tests/ -v

e2e:
	python3 e2e/smoke.py

clean:
	rm -f microblog.db
	find . -name '__pycache__' -type d -prune -exec rm -rf {} +
