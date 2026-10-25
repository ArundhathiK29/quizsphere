FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    cppcheck \
    libgtest-dev \
    libsqlite3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Build and test as a normal user: permission-based tests behave differently as root.
RUN useradd --create-home builder
WORKDIR /workspace
RUN chown builder:builder /workspace
COPY --chown=builder:builder . .
USER builder

RUN cmake -S . -B build -G Ninja -DQUIZSPHERE_BUILD_TESTS=ON \
    && cmake --build build \
    && cppcheck --enable=warning,style,performance,portability --error-exitcode=1 -I include include src

CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
