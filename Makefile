CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 -g
MT_INCLUDE := $(HOME)/repos/mt/include
MT_CODEGEN := $(HOME)/repos/mt/tools/mt_codegen.py
PYTHON ?= python3
CPPFLAGS := -Iinclude -Isrc -Ithird_party -I$(MT_INCLUDE)

FORMAT := clang-format
PLANTUML ?= plantuml

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
DOCS_DIR := docs

LIB_NAME := libqu.a
LIB := $(BUILD_DIR)/$(LIB_NAME)
TEST_BIN := $(BIN_DIR)/qu_tests

SRC := $(shell find src -name '*.cpp' | sort)
TEST_SRC := $(shell find tests -name '*.cpp' | sort)
HEADER_FILES := $(shell find include -name '*.hpp' | sort)
PRIVATE_HEADER_FILES := $(shell find src/tables -name '*.hpp' | sort)
TABLE_SCHEMA_FILES := $(shell find src/tables/schemas -name '*.mt.json' | sort)
GENERATED_TABLE_HEADERS := \
	src/tables/generated/queue_channel_counter_row.hpp \
	src/tables/generated/queue_message_row.hpp
CODEGEN_CHECK_DIR := $(BUILD_DIR)/codegen-check
PUML_FILES := $(shell find $(DOCS_DIR) -name '*.puml' 2>/dev/null | sort)
DOC_PNG_FILES := $(PUML_FILES:.puml=.png)

ifneq ($(filter 1 true yes,$(FORCE_CODEGEN) $(FORCE)),)
.PHONY: $(GENERATED_TABLE_HEADERS)
endif

CATCH_SRC := third_party/catch2/catch_amalgamated.cpp

OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRC))
TEST_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(TEST_SRC))
CATCH_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CATCH_SRC))

FORMAT_FILES := $(HEADER_FILES) $(PRIVATE_HEADER_FILES) $(SRC) $(TEST_SRC)

.PHONY: all build test codegen codegen-force codegen-check docs-png format format-check clean clean-docs help

all: test

build: $(LIB)

codegen: $(GENERATED_TABLE_HEADERS)

codegen-force:
	$(MAKE) codegen FORCE_CODEGEN=1

codegen-check:
	@mkdir -p $(CODEGEN_CHECK_DIR)
	$(PYTHON) $(MT_CODEGEN) src/tables/schemas/queue_channel_counter.mt.json -o $(CODEGEN_CHECK_DIR)/queue_channel_counter_row.hpp
	$(FORMAT) -i $(CODEGEN_CHECK_DIR)/queue_channel_counter_row.hpp
	diff -u src/tables/generated/queue_channel_counter_row.hpp $(CODEGEN_CHECK_DIR)/queue_channel_counter_row.hpp
	$(PYTHON) $(MT_CODEGEN) src/tables/schemas/queue_message.mt.json -o $(CODEGEN_CHECK_DIR)/queue_message_row.hpp
	$(FORMAT) -i $(CODEGEN_CHECK_DIR)/queue_message_row.hpp
	diff -u src/tables/generated/queue_message_row.hpp $(CODEGEN_CHECK_DIR)/queue_message_row.hpp

docs-png: $(DOC_PNG_FILES)

$(DOCS_DIR)/%.png: $(DOCS_DIR)/%.puml
	$(PLANTUML) -tpng $<

src/tables/generated/queue_channel_counter_row.hpp: src/tables/schemas/queue_channel_counter.mt.json
	$(PYTHON) $(MT_CODEGEN) $< -o $@
	$(FORMAT) -i $@

src/tables/generated/queue_message_row.hpp: src/tables/schemas/queue_message.mt.json
	$(PYTHON) $(MT_CODEGEN) $< -o $@
	$(FORMAT) -i $@

$(LIB): codegen format $(OBJ)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $(OBJ)

$(TEST_BIN): $(LIB) $(TEST_OBJ) $(CATCH_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(TEST_OBJ) $(CATCH_OBJ) $(LIB)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

test: $(TEST_BIN)
	$(TEST_BIN)

format:
	$(FORMAT) -i $(FORMAT_FILES)

format-check:
	$(FORMAT) --dry-run --Werror $(FORMAT_FILES)

clean:
	rm -rf $(BUILD_DIR)

clean-docs:
	rm -f $(DOC_PNG_FILES)

help:
	@echo "Targets:"
	@echo "  make              Build and run tests"
	@echo "  make build        Build static library only"
	@echo "  make test         Build and run tests"
	@echo "  make codegen      Generate private mt row and mapping headers"
	@echo "  make codegen-force Force-regenerate private mt row and mapping headers"
	@echo "  make codegen-check Verify generated mt row headers are current"
	@echo "  make docs-png     Generate PNG diagrams from docs/*.puml"
	@echo "  make format       Format source and header files with clang-format"
	@echo "  make format-check Check formatting without modifying files"
	@echo "  make clean        Remove build outputs"
	@echo "  make clean-docs   Remove generated docs/*.png diagrams"

DEP_FILES := $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(CATCH_OBJ:.o=.d)
-include $(DEP_FILES)
