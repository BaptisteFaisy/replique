# Makefile — Réplique (machine à remonter le temps pour QNX 8.0)
#
# `make test` est vert SANS QNX, sans cible et sans licence (exigence S8) :
# le coeur C11 de core/ et le jumeau Python de host/ se suffisent à eux-mêmes.
# Le code cible (Déclic) se construit à part : `make target-qnx` (SDP requis).

CC       ?= gcc
CFLAGS   ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS += -Icore/include -Itarget/declic
PYTHON   ?= python3
AR       ?= ar
BUILD    := build
PY       := PYTHONPATH=host $(PYTHON)

CORE_SRC    := $(wildcard core/src/*.c)
CORE_OBJ    := $(patsubst core/src/%.c,$(BUILD)/obj/%.o,$(CORE_SRC))
LIB         := $(BUILD)/libreplique.a
TEST_SRC    := $(wildcard core/tests/test_*.c)
TEST_BIN    := $(patsubst core/tests/test_%.c,$(BUILD)/test_%,$(TEST_SRC))
DRIVERS     := $(BUILD)/kev-dump $(BUILD)/analyze $(BUILD)/replay-dump
TARGET_SRC  := $(wildcard target/declic/*_portable.c)
TARGET_OBJ  := $(patsubst target/declic/%.c,$(BUILD)/tobj/%.o,$(TARGET_SRC))
# Déclic (logique cible portable) n'arrive qu'avec le lot target/ : la cible
# n'est incluse que si sa source existe, pour que `make build` reste vert.
DECIC_TEST  := $(if $(wildcard core/tests/declic_test.c),$(BUILD)/declic-test,)
DRIVER_IO   := core/tests/driver_io.c

.PHONY: all build test unit differential determinism fixtures guard clean help \
        target-qnx oracle
.SILENT: unit

all: build

help:
	@echo "cibles : build test unit differential determinism fixtures guard oracle clean target-qnx"

$(BUILD)/.keep $(BUILD)/obj $(BUILD)/tobj:
	mkdir -p $(BUILD) $(BUILD)/obj $(BUILD)/tobj
	@touch $(BUILD)/.keep

$(BUILD)/obj/%.o: core/src/%.c | $(BUILD)/obj
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(LIB): $(CORE_OBJ)
	$(AR) rcs $@ $(CORE_OBJ)

$(BUILD)/test_%: core/tests/test_%.c $(LIB) | $(BUILD)/.keep
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB) -o $@

$(BUILD)/kev-dump: core/tests/dump_events.c $(DRIVER_IO) $(LIB) | $(BUILD)/.keep
	$(CC) $(CFLAGS) $(CPPFLAGS) -Icore/tests $< $(DRIVER_IO) $(LIB) -o $@

$(BUILD)/analyze: core/tests/dump_pipeline.c $(DRIVER_IO) $(LIB) | $(BUILD)/.keep
	$(CC) $(CFLAGS) $(CPPFLAGS) -Icore/tests $< $(DRIVER_IO) $(LIB) -o $@

$(BUILD)/replay-dump: core/tests/dump_replay.c $(DRIVER_IO) $(LIB) | $(BUILD)/.keep
	$(CC) $(CFLAGS) $(CPPFLAGS) -Icore/tests $< $(DRIVER_IO) $(LIB) -o $@

$(BUILD)/tobj/%.o: target/declic/%.c | $(BUILD)/tobj
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# declic_test.c ne suit pas le motif test_*.c pour ne pas etre aspire
# par la regle generique (il a besoin des objets de target/).
# La logique portable de Déclic (archive, manifeste, décision de déclenchement)
# est testée sur PC ; la colle QNX (*_qnx.c) ne se compile qu'avec le SDP.
$(DECIC_TEST): core/tests/declic_test.c $(TARGET_OBJ) $(LIB) | $(BUILD)/.keep
	$(CC) $(CFLAGS) $(CPPFLAGS) core/tests/declic_test.c $(TARGET_OBJ) $(LIB) -o $@

build: $(LIB) $(TEST_BIN) $(DRIVERS) $(DECIC_TEST)

fixtures:
	$(PY) -m replique.cli gen --all --out fixtures

unit: build $(DECIC_TEST)
	for t in $(TEST_BIN) $(DECIC_TEST); do \
	  echo "--- $$t"; $$t || exit 1; \
	done
	@echo "unit OK ($$(echo $(TEST_BIN) $(DECIC_TEST) | wc -w) binaires)"

differential: build fixtures
	tests/differential_c_python.sh

determinism: build fixtures
	tests/determinism.sh

oracle: build fixtures
	tests/differential_traceprinter.sh

guard:
	$(PYTHON) .github/checks/size_guard.py
	$(PYTHON) .github/checks/purity_guard.py
	$(PYTHON) .github/ci-budget/check_timeouts.py

test: guard build fixtures unit differential determinism
	@echo "MAKE TEST VERT — sans QNX, sans cible, sans licence (S8)"

# Hors CI : nécessite le SDP QNX 8.0 (qcc + mkifs). Jamais requis par `make test`.
target-qnx:
	@if command -v qcc >/dev/null 2>&1; then \
	  $(MAKE) -C target/declic -f Makefile.qnx; \
	else \
	  echo "qcc absent : Déclic ne se compile qu'avec le SDP QNX 8.0 (voir docs/DESIGN.md §7)."; \
	  echo "La logique portable, elle, est testée ici : make test (cible declic-test)."; \
	fi

clean:
	rm -rf $(BUILD)
