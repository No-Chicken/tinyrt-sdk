PYTHON ?= python
APP ?= examples/flappy
RELEASE_ARGS ?= --development-key

.PHONY: release
release:
	$(PYTHON) tools/tinyrt.py release "$(APP)" $(RELEASE_ARGS)
