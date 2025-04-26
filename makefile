# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++23 -Wall -Wextra -O3
LDFLAGS := -Lbenchmark/build/src -lbenchmark -lpthread
INCLUDES := -isystem benchmark/include

# Source files and executable
SRC := merge_sort.cpp
TARGET := merge_sort
BENCHMARK_OUTPUT := benchmark.csv

# Default target
all: $(TARGET)

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

# Phony targets
.PHONY: all run clean