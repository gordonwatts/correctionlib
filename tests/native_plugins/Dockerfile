FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake make ca-certificates zlib1g-dev && rm -rf /var/lib/apt/lists/*
COPY . /src
RUN ! command -v python && ! command -v python3 && test ! -e /src/pybind11/CMakeLists.txt \
    && cmake -S /src -B /build -DCORRECTIONLIB_BUILD_PYTHON=OFF -DBUILD_DEMO=OFF \
       -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/installed \
    && cmake --build /build -j2 && cmake --install /build \
    && cmake -S /src/examples/node_plugin -B /provider -DCMAKE_PREFIX_PATH=/installed \
       -DCMAKE_INSTALL_PREFIX=/installed \
    && cmake --build /provider -j2 && cmake --install /provider \
    && cmake -S /src/examples/node_plugin/consumer -B /standalone -DCMAKE_PREFIX_PATH=/installed \
    && cmake --build /standalone -j2 && /standalone/native_plugin_consumer \
    && cmake -S /src/tests/native_plugins -B /consumer -DCMAKE_PREFIX_PATH=/installed \
    && cmake --build /consumer -j2 && ctest --test-dir /consumer --output-on-failure \
    && ! ldd /installed/correctionlib/lib/libcorrectionlib.so | grep -i python \
    && ! ldd /consumer/test_native_plugins | grep -i python \
    && ! command -v python3
