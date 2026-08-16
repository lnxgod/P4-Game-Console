.PHONY: check firmware

HOST_BUILD_DIR ?= build-host

check:
	python3 -m unittest discover -s tests -p 'test_*.py'
	cmake -S . -B $(HOST_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(HOST_BUILD_DIR) --parallel
	ctest --test-dir $(HOST_BUILD_DIR) --output-on-failure

firmware:
	./scripts/build-firmware.sh
