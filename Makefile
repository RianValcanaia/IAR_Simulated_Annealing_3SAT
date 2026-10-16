.PHONY: build mpi clean venv

# fontes comuns ao binário e ao programa MPI (sem os dois main)
LIB := $(filter-out src/main.cpp src/sa3sat_mpi.cpp,$(wildcard src/*.cpp))

build:
	@echo "Building the binary..."
	mkdir -p bin
	g++ -Wall -std=c++17 -O2 -Wextra -o bin/sa3sat src/main.cpp $(LIB)

# roda com: mpirun -np 27 --machinefile hosts.txt ./bin/sa3sat_mpi
mpi:
	@echo "Building the MPI campaign..."
	mkdir -p bin
	mpicxx -Wall -std=c++17 -O2 -Wextra -DOMPI_SKIP_MPICXX -DMPICH_SKIP_MPICXX -Isrc -o bin/sa3sat_mpi src/sa3sat_mpi.cpp $(LIB)

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
