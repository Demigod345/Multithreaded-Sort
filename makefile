# Compiler and flags
CXX := clang++
CXXFLAGS := -std=c++23 -Wall -Wextra -O3 -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include
LDFLAGS := -Lbenchmark/build/src -lbenchmark -lpthread -L/opt/homebrew/opt/libomp/lib -lomp
INCLUDES := -isystem benchmark/include

# Source files and executable
SRC := merge_sort.cpp
TARGET := merge_sort
BENCHMARK_OUTPUT := benchmark.csv

# Default target
all: install_libomp benchmark_library $(TARGET)

# Install OpenMP if not installed
install_libomp:
	@if ! brew list libomp &>/dev/null; then \
		echo "Installing OpenMP (libomp) via Homebrew..."; \
		brew install libomp; \
	else \
		echo "OpenMP (libomp) already installed."; \
	fi

# Build Google Benchmark library if not already built
benchmark_library:
	@if [ ! -f benchmark/build/src/libbenchmark.a ]; then \
		echo "Building Google Benchmark library..."; \
		cmake -S benchmark -B benchmark/build -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_ENABLE_GTEST_TESTS=OFF; \
		cmake --build benchmark/build --config Release; \
	else \
		echo "Google Benchmark already built."; \
	fi

# Build target
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDFLAGS)

# Run benchmarks and output to CSV
run: $(TARGET)
	./$(TARGET) --benchmark_format=csv > $(BENCHMARK_OUTPUT)
	@echo "Benchmark results saved to $(BENCHMARK_OUTPUT)"

# Clean build artifacts
clean:
	rm -f $(TARGET) $(BENCHMARK_OUTPUT)

# Clean everything including benchmark library
cleanall: clean
	rm -rf benchmark/build

# Phony targets
.PHONY: all run clean cleanall benchmark_library install_libomp
