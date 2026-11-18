# MatmMax 8086 DOS build. Run from this directory.
APP ?= m01_mathsmith
.DEFAULT_GOAL := all

WATCOM ?= /opt/open-watcom
export WATCOM
export INCLUDE := $(WATCOM)/h
export PATH := $(WATCOM)/binl64:$(WATCOM)/binl:$(PATH)

CC_NATIVE ?= gcc
PYTHON    ?= python3

APP_DIR   := apps/$(APP)
BUILD_DIR := build/$(APP)

ifeq ($(wildcard $(APP_DIR)/app.mk),)
$(error Unknown APP '$(APP)'. Available: $(patsubst apps/%/app.mk,%,$(wildcard apps/*/app.mk apps/*/*/app.mk)))
endif
include $(APP_DIR)/app.mk

# Use 8086 instructions and Watcom's 16-bit DOS linker.
WCC := wcc
WCC_FLAGS := -bt=dos -m$(MODEL) -0 -os -zq -wx -Icommon $(if $(ALLOW_WARNINGS),,-we) $(EXTRA_WCC)
LINK_SYSTEM := system dos
NATIVE_FLAGS := -std=c89 -Icommon -Wall -Wextra $(if $(ALLOW_WARNINGS),,-Werror) -shared -fPIC $(NATIVE_EXTRA_FLAGS)

OBJECTS    := $(patsubst %.c,$(BUILD_DIR)/%.obj,$(SOURCES)) \
              $(patsubst %.c,$(BUILD_DIR)/%.obj,$(COMMON_SOURCES))
EXE        := $(BUILD_DIR)/$(EXE_NAME).EXE
NATIVE_LIB := $(BUILD_DIR)/native.so
EXTRA_SRC_DIRS ?=
HEADERS    := $(wildcard $(APP_DIR)/*.h common/*.h $(addsuffix /*.h,$(EXTRA_SRC_DIRS)))

.PHONY: all test

all: $(EXE)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.obj: $(APP_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(WCC) $(WCC_FLAGS) -fo=$@ $<

$(BUILD_DIR)/%.obj: common/%.c $(HEADERS) | $(BUILD_DIR)
	$(WCC) $(WCC_FLAGS) -fo=$@ $<

# app.mk may name more source directories (EXTRA_SRC_DIRS), e.g. a program that shares another program's engine.
define EXTRA_DIR_RULE
$$(BUILD_DIR)/%.obj: $(1)/%.c $$(HEADERS) | $$(BUILD_DIR)
	$$(WCC) $$(WCC_FLAGS) -fo=$$@ $$<
endef
$(foreach d,$(EXTRA_SRC_DIRS),$(eval $(call EXTRA_DIR_RULE,$(d))))


$(NATIVE_LIB): $(addprefix $(APP_DIR)/,$(NATIVE_SOURCES)) $(addprefix common/,$(NATIVE_COMMON_SOURCES)) $(HEADERS) | $(BUILD_DIR)
	$(CC_NATIVE) $(NATIVE_FLAGS) -o $@ $(filter %.c,$^)

test: $(EXE) $(if $(NATIVE_SOURCES)$(NATIVE_COMMON_SOURCES),$(NATIVE_LIB))
	XT_EXE=$(EXE) XT_NATIVE_LIB=$(NATIVE_LIB) PYTHONPATH=common/testlib \
	  $(PYTHON) -m unittest discover -s $(APP_DIR)/tests -v

$(EXE): $(OBJECTS) $(EXTRA_LIBS)
	wlink option quiet $(LINK_SYSTEM) name $@ option map=$(BUILD_DIR)/$(EXE_NAME).MAP $(if $(STACK),option stack=$(STACK)) \
	  $(foreach o,$(OBJECTS),file $(o)) $(foreach l,$(EXTRA_LIBS),library $(l))
