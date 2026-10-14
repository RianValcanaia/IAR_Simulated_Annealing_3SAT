.PHONY: build clean venv

build:
	@echo "Building the binary..."
	mkdir -p bin
	g++ -Wall -std=c++17 -O2 -Wextra -o bin/sa3sat src/*.cpp 

clean:
	@echo "Cleaning up..."
	rm -rf bin/
	rm -rf .venv/
	rm -rf scripts/__pycache__/

venv: .venv/.instalado
	@echo "Virtual environment is ready."

.venv/.instalado: scripts/requirements.txt
	python3 -m venv .venv
	.venv/bin/pip install -r scripts/requirements.txt
	@touch $@
