config:
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

build:
    cmake --build build

run:
    cmake --build build && ./build/mksite

watch:
    watchexec -c -r -e h,c,txt,css "cmake --build build && ./build/mksite"
