include .env

.PHONY: all image run build clean clean-build clean-logs
.SILENT: clean

COMPILER ?= mpicc
CFLAGS ?= -O2 -g -Wall -Wno-variadic-macros -pedantic
BUILD_DIR := bin
LOGS_DIR := logs
HARNESS_BUILD_DIR ?= $(CURDIR)/$(BUILD_DIR)
PLAYER_BASE_VERSION ?= $(VERSION)

all: run 

# --- Process mode ---

$(BUILD_DIR):
	mkdir -p $@

$(LOGS_DIR):
	mkdir -p $@

build: | $(BUILD_DIR) $(LOGS_DIR)
	$(MAKE) -C "$(HARNESS_DIR)" BUILD_DIR="$(HARNESS_BUILD_DIR)"
	$(COMPILER) $(CFLAGS) -o "$(BUILD_DIR)/player.o" -c my_player/src/player.c -I"$(HARNESS_DIR)/src"
	$(COMPILER) -o "$(BUILD_DIR)/myplayer" "$(BUILD_DIR)"/*.o -lm

run: build
	mpirun -np 2 --oversubscribe "$(BUILD_DIR)/myplayer" 1.0 61235 4 0

clean-build:
	rm -rf $(BUILD_DIR)

clean-logs:
	rm -rf logs/

# --- Docker image ---

image:
	docker build --network=host --build-arg REGISTRY=$(REGISTRY) --build-arg VERSION=$(VERSION) --build-arg BASE_VERSION=$(PLAYER_BASE_VERSION) -f my_player/Dockerfile -t $(REGISTRY)/my-player:$(VERSION) my_player/src

clean:
	docker rmi -f $(REGISTRY)/my-player:$(VERSION)
	docker builder prune
	rm -rf bin/*
