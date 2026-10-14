.PHONY: build clean

build:
	@echo "Building the binary..."
	mkdir -p bin
	g++ -Wall -std=c++17 -O2 -Wextra -o bin/sa3sat src/*.cpp 

clean:
	@echo "Cleaning up..."
	rm -rf bin/