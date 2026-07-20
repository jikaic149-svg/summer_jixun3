.PHONY: debug release clean

debug:
	cmake --preset debug
	cmake --build --preset debug

release:
	cmake --preset release
	cmake --build --preset release

clean:
	cmake -E remove_directory build/cmake
