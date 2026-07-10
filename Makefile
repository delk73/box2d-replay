.PHONY: replay-check

replay-check:
	cmake -S . -B build/replay-check \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBOX2D_SAMPLES=OFF \
		-DBOX2D_UNIT_TESTS=OFF \
		-DBOX2D_BENCHMARKS=OFF \
		-DBOX2D_DISABLE_SIMD=ON
	cmake --build build/replay-check --target replay_check
	@echo "make replay-check: PASS"
