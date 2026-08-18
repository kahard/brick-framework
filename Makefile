.PHONY: format format-check test

format:
	bash ./tools/clang.sh format

format-check:
	bash ./tools/clang.sh format-check

test:
	cmake --build build --config Debug
	ctest --test-dir build --output-on-failure
