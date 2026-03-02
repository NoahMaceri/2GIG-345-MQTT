FROM debian:bookworm-slim AS builder

RUN apt-get -q update && apt-get install -yq --no-install-recommends \
	build-essential librtlsdr-dev pkg-config libmosquittopp-dev cmake libyaml-cpp-dev libspdlog-dev \
	&& apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /345tomqtt

COPY . /345tomqtt

RUN cmake -B build -S . && cmake --build build

FROM debian:bookworm-slim

RUN apt-get -q update && apt-get install -yq --no-install-recommends \
	librtlsdr0 rtl-sdr libmosquittopp1 libspdlog1.10 libyaml-cpp0.8 \
	&& apt-get clean && rm -rf /var/lib/apt/lists/*

COPY --from=builder /345tomqtt/build/345toMqtt /345toMqtt
COPY ./config.yaml /config.yaml

CMD ["/345toMqtt", "-c", "/config.yaml"]
