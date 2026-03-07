![License](https://img.shields.io/github/license/NoahMaceri/2GIG-345-MQTT)
![Language](https://img.shields.io/github/languages/top/NoahMaceri/2GIG-345-MQTT)
![Last Commit](https://img.shields.io/github/last-commit/NoahMaceri/2GIG-345-MQTT)
![Issues](https://img.shields.io/github/issues/NoahMaceri/2GIG-345-MQTT)
![Docker](https://img.shields.io/badge/docker-supported-blue?logo=docker)

# 2GIG 345MHz Sensor MQTT Bridge

A lightweight C++ bridge that captures 345MHz wireless security sensor signals using an RTL-SDR dongle and publishes device states to an MQTT broker — making 2GIG/Honeywell door sensors, keypads, and key fobs available to any home automation system.

Based on [vondruska's 345SecurityMQTT](https://github.com/vondruska/345SecurityMQTT), which is based on [fusterjj's HoneywellSecurityMQTT](https://github.com/fusterjj/HoneywellSecurityMQTT) which is based on [jhaines0's HoneywellSecurity](https://github.com/jhaines0/HoneywellSecurity).

## Features

- **Door/window sensors** — open/closed state, tamper detection, battery monitoring
- **Keypads** — individual key presses and multi-key phrase recognition
- **Key fobs** — STAY, AWAY, DISARM, AUX button presses
- **Supervision tracking** — detects when devices stop reporting
- **Retained MQTT messages** — sensor states persist across broker restarts
- **Structured logging** — timestamped logs via spdlog with configurable log level
- **Graceful shutdown** — clean exit on SIGINT/SIGTERM
- **Configurable topics** — customizable MQTT topic prefix
- **Docker support** — multi-stage build on Debian slim

## Requirements

- RTL-SDR USB dongle (RTL2832U-based, e.g. NooElec or similar)
  - Ensure that you have installed the necessary drivers and have permissions to access the device (e.g. via `udev` rules on Linux)
- MQTT broker (Mosquitto, EMQX, etc.)

## Quick Start (Docker)

```bash
docker compose up -d
```

Or without Compose:

```bash
docker build -t 345tomqtt .
docker run --device /dev/bus/usb -v ./config.yaml:/config.yaml 345tomqtt
```

## Building from Source

### Dependencies

```bash
sudo apt-get install cmake build-essential librtlsdr-dev libmosquittopp-dev libyaml-cpp-dev libspdlog-dev
```

### Build

```bash
cmake -B build -S .
cmake --build build
```

### Run

```bash
./build/345toMqtt
```

**Options:**

| Flag | Description | Default |
|------|-------------|---------|
| `-c <path>` | Path to config file | `config.yaml` |

## Configuration

Edit `config.yaml`:

```yaml
mqtt:
  host: 127.0.0.1
  port: 1883
  username: user
  password: password
  topic_prefix: security/sensors345

rtlsdr:
  device_id: 0
  frequency: 345000000
  gain: 490
  sample_rate: 1000000

log_level: info
```

All fields have sensible defaults if omitted.

## MQTT Topics

The default topic prefix is `security/sensors345`. This is configurable via `mqtt.topic_prefix` in `config.yaml`.

| Topic | Payload | Retain |
|-------|---------|--------|
| `<prefix>/sensor/<txid>/loop<N>` | `OPEN` / `CLOSED` | Yes |
| `<prefix>/sensor/<txid>/tamper` | `TAMPER` / `OK` | Yes |
| `<prefix>/sensor/<txid>/battery` | `LOW` / `OK` | Yes |
| `<prefix>/keypad/<txid>/keypress` | `0`-`9`, `*`, `#`, `STAY`, `AWAY`, `FIRE`, `POLICE` | No |
| `<prefix>/keypad/<txid>/keyphrase/<LEN>` | `[*#0-9]{2,}` (keys entered within 2s) | No |
| `<prefix>/keyfob/<txid>/keypress` | `STAY`, `AWAY`, `DISARM`, `AUX` | No |
| `<prefix>/rx_status` | `OK` / `FAILED` | Yes |
| `<prefix>/diagnostics/error_rate` | `<errors>/<total>` | Yes |

## License

[Apache 2.0](LICENSE)
