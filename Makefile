ifeq ($(OS),Windows_NT)
BOOTSTRAP_PYTHON ?= py -3
PYTHON := .venv/Scripts/python.exe
else
BOOTSTRAP_PYTHON ?= python3
PYTHON := .venv/bin/python
endif

.PHONY: setup format format-check configure test

# Create .venv with the pinned clang-format and the asset tool dependencies.
setup:
	$(BOOTSTRAP_PYTHON) -m venv .venv
	$(PYTHON) -m pip install --disable-pip-version-check -q -r requirements.txt

format:
	$(PYTHON) tools/format.py format

format-check:
	$(PYTHON) tools/format.py check

configure:
	cmake -S . -B build -DBRICK_BUILD_TESTS=ON

test:
	cmake --build build --config Debug
	ctest --test-dir build --output-on-failure
