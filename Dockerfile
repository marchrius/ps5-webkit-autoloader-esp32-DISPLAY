FROM node:22-bookworm

# GNU tools are required by the Makefile and prepare-data.sh.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl git gzip jq openssl \
        python3 python3-venv unzip xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Supply dependencies for the bundled esptool without modifying system Python.
ENV PATH="/opt/venv/bin:${PATH}"
RUN python3 -m venv /opt/venv \
    && pip install --no-cache-dir esptool==4.5.1 PyYAML

# Pin the CLI too, so builds use the same version on every host.
ARG ARDUINO_CLI_VERSION=1.2.2
ARG TARGETARCH
RUN case "$TARGETARCH" in \
        amd64) cli_arch=64bit ;; \
        arm64) cli_arch=ARM64 ;; \
        *) echo "Unsupported architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac \
    && curl -fsSL \
        "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_${cli_arch}.tar.gz" \
        -o /tmp/arduino-cli.tar.gz \
    && tar -xzf /tmp/arduino-cli.tar.gz -C /usr/local/bin arduino-cli \
    && rm /tmp/arduino-cli.tar.gz

# Keep toolchains in the image, outside the mounted source directory.
COPY install-deps.sh /tmp/install-deps.sh
RUN sed -i 's/\r$//' /tmp/install-deps.sh \
    && bash /tmp/install-deps.sh \
    && rm /tmp/install-deps.sh \
    && rm -rf /var/lib/apt/lists/* /root/.arduino15/staging/*

# Login shells (including editor terminals) must also find the Python venv.
RUN echo 'export PATH="/opt/venv/bin:$PATH"' > /etc/profile.d/python-venv.sh

WORKDIR /workspace
CMD ["bash"]
