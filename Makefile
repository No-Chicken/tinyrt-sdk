PYTHON ?= python
APP ?= examples/flappy
RELEASE_ARGS ?=

.PHONY: release
release:
	$(PYTHON) tools/tinyrt.py release "$(APP)" $(RELEASE_ARGS)
