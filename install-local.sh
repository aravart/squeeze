cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp build/libsqueeze_ffi.dylib python/squeeze/
cd python && pip install -e .
