# WeBjammin — build
#
# Plain make (no cmake: the Cardputer Zero has gcc 14.2 + make, no cmake).
# Targets:
#   make            → build/webjammin            (host build)
#   make device     → build/webjammin-arm64      (device build, run on the unit)
#   make test       → build/webjammin-tests       (unit tests, host)
#   make run        → build/webjammin --selftest
#   make clean

CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE \
             -Isrc -Isrc/util -Isrc/chart -Isrc/audio -Isrc/input -Isrc/render \
             -Isrc/game -Isrc/app
LDLIBS   ?= -lm -lpthread -lasound

BUILD    := build
BIN      := $(BUILD)/webjammin
DEV_BIN  := $(BUILD)/webjammin-arm64

SRC := $(shell find src -name '*.cpp' -not -name 'main.cpp')
OBJ := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRC))
DEV_OBJ := $(patsubst src/%.cpp,$(BUILD)/arm64/%.o,$(SRC))

TEST_SRC := $(shell find tests -name '*.cpp')
TEST_OBJ := $(patsubst tests/%.cpp,$(BUILD)/tests/%.o,$(TEST_SRC))
TEST_BIN := $(BUILD)/webjammin-tests

.PHONY: all device test run clean songs

all: $(BIN)

device: $(DEV_BIN)

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/arm64/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN): $(OBJ) $(BUILD)/main.o
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(DEV_BIN): $(DEV_OBJ) $(BUILD)/arm64/main.o
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJ) $(filter-out $(BUILD)/main.o,$(OBJ))
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

run: $(BIN)
	./$(BIN) --selftest

# Regenerate the bundled procedural songs into songs/.
songs: $(BIN)
	./$(BIN) --generate-songs songs

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
