FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make libgtest-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY headers ./headers
COPY src ./src
COPY app ./app
COPY native ./native
COPY tests ./tests
COPY resources ./resources
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build -j2 \
    && ctest --test-dir /build --output-on-failure

FROM debian:bookworm-slim AS base
RUN apt-get update && apt-get install -y --no-install-recommends python3 libstdc++6 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 simulator && useradd --uid 10001 --gid 10001 --no-create-home simulator
WORKDIR /app
COPY app ./app
USER 10001:10001
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
HEALTHCHECK --interval=10s --timeout=5s --start-period=10s --retries=5 \
    CMD python3 -c "import os,urllib.request; urllib.request.urlopen('http://127.0.0.1:'+os.environ.get('PORT','8080')+'/health', timeout=4)"

FROM base AS ecu
COPY --from=build /build/ecu_kernel /usr/local/bin/ecu_kernel
ENV ECU_BINARY=/usr/local/bin/ecu_kernel PORT=8081
EXPOSE 8081
CMD ["python3", "app/worker.py"]

FROM base AS simulator
COPY --from=build /build/vehicle_sim /usr/local/bin/vehicle_sim
COPY --from=build /build/vclab /usr/local/bin/vclab
COPY --from=build /build/vclab_worker /usr/local/bin/vclab_worker
COPY resources ./resources
COPY web ./web
ENV SIM_BINARY=/usr/local/bin/vehicle_sim PORT=8080
EXPOSE 8080
CMD ["python3", "app/server.py"]

# Optional fully native worker service. Existing Python-backed demo targets remain.
FROM debian:bookworm-slim AS native-ecu
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 simulator && useradd --uid 10001 --gid 10001 --no-create-home simulator
COPY --from=build /build/vclab_worker /usr/local/bin/vclab_worker
USER 10001:10001
EXPOSE 8081
CMD ["vclab_worker", "--http", "8081", "0.0.0.0"]
