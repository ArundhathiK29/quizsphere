FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    cppcheck \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . .

RUN cmake -S . -B build -G Ninja -DQUIZSPHERE_BUILD_TESTS=ON \
    && cmake --build build \
    && cppcheck --enable=warning,style,performance,portability --error-exitcode=1 include src

CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
