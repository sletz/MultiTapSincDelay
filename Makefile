# Makefile for project compilation

.PHONY: all clean test help

# Build section
all:
	@c++ MultiTapSincDelay.cpp -o MultiTapSincDelayCpp
	@faust2plot MultiTapSincDelay.dsp

# Test section
test:
	./MultiTapSincDelayCpp > cpp.log
	./MultiTapSincDelay -n 1000 > faust.log
		
# Clean build directories
clean:
	@echo "Cleaning binaries and logs..."
	@rm -f MultiTapSincDelayCpp MultiTapSincDelay *.log
	
# Format code
format:
	@echo "Formatting CPP source code..."
	@find . -iname '*.cpp' -execdir clang-format -i -style=file {} \;
	
# Help section
help:
	@echo "Available targets:"
	@echo "  all       - Build for C++ and Faust"
	@echo "  test      - Run C++ and Faust and keep logs"
	@echo "  format    - Format C++ code"
	@echo "  clean     - Remove binaries and logs"
