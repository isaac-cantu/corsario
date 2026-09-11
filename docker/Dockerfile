# corsario: fast CORSIKA .DAT reader — server / batch conversion image
#
# Build:
#   docker build -t corsario -f docker/Dockerfile .
# Run:
#   docker run --rm -v /data/corsika:/data corsario \
#       corsario-convert /data/DAT000001 /data/particles.csv
# Or drop into a shell with the library available:
#   docker run --rm -it -v /data/corsika:/data corsario bash

FROM python:3.11-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN pip install --no-cache-dir build && \
    python -m build --wheel --outdir /wheels .

# ---------------------------------------------------------------------------
FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /wheels /wheels
RUN pip install --no-cache-dir /wheels/*.whl "h5py>=3.0" pandas numpy

WORKDIR /data
ENTRYPOINT []
CMD ["bash"]
