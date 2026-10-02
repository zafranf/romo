# Romo

**Romo** is a free, open-source MongoDB client for macOS, Windows and Linux —
a continuation of **Robo 3T** (formerly Robomongo), rebuilt for modern servers
and modern networks.

Romo is a fork of [Studio 3T's Robo 3T](https://github.com/Studio3T/robomongo).
Everything you know from Robo 3T is kept: the embedded MongoDB shell,
IntelliSense, the Explorer tree, in-app scripting, import/export, and direct
or Replica Set connections.

## What's new compared to Robo 3T

- **SSH tunnels for Replica Set connections** — Romo opens one local tunnel per
  member, so clusters on private networks just work. Robo 3T disabled the SSH
  tab whenever Replica Set mode was selected.
- **Modern OpenSSH support** — bundled libssh2 was upgraded from 1.9 to 1.11,
  adding `rsa-sha2`. Robo 3T could no longer authenticate against OpenSSH 8.8+
  servers that dropped SHA-1.
- **Command history** — press Up/Down in the console to recall what you ran
  earlier, and browse/search everything in the *View > History & Favorites*
  panel, with Paste and Run actions on every command.
- **Starred favorites** — save queries under a name, edit them later
  (*View & Edit*), star or unstar with one click. Favorites are curated
  manually and never expire or get trimmed.
- **Zero telemetry** — no signup form, no blog RSS, no update pings. Romo works
  fully offline and never sends data anywhere.
- **Native Apple Silicon build** — compiled for arm64 with a current toolchain.

## Requirements

- CMake, a C++17 compiler (Xcode Command Line Tools on macOS)
- Qt 5.15 (WebEngine is **not** required — it is disabled outside Windows)
- OpenSSL 1.1.x
- Python 3 + SCons (for the bundled MongoDB shell)
- ~5 GB of free disk for the build tree

## Building from source

The build needs two repositories checked out side by side:

```
<parent>/
├── roborobo/        # this repository — the application
└── roborobo-shell/  # MongoDB shell 4.2 fork (patched, built separately)
```

### 1. Build the MongoDB shell

```bash
cd roborobo-shell
python3 -m venv .venv
.venv/bin/pip install -r etc/pip/compile-requirements.txt 'scons==4.8.1' 'setuptools==75.8.0'
.venv/bin/scons mongo --ssl --disable-warnings-as-errors MONGO_VERSION=4.2.6 \
  --link-model=object \
  CCFLAGS="-mmacosx-version-min=10.13 -Wno-unused-function" \
  LINKFLAGS="-mmacosx-version-min=10.13 -Wno-unused-function" -j4
```

`--link-model=object` matters: the app links the resulting `.o` files directly
(see `cmake/mongodb/macosx-release.objects`).

### 2. Build the app

```bash
cd roborobo
export ROBOMONGO_CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5;$PWD/../roborobo-shell;$PWD/../openssl-1.1.1w"
bin/configure
bin/build
bin/install     # produces build/release/install/Romo.app (bundles + signs OpenSSL)
open build/release/install/Romo.app
```

## Data & configuration

Everything lives in a single flat directory — no per-version folders, so your
connections survive upgrades:

```
~/.romo/
├── romo.json        # connections, preferences, EULA state
├── romo.key         # encryption key for stored passwords (keep a backup!)
├── history.jsonl    # command history (capped at 500 unique entries per connection)
└── favorites.jsonl  # starred favorites (never trimmed)
```

## License

Romo is licensed under the **GNU General Public License v3** (see `LICENSE`).

Bundled components and their licenses:

| Component        | License                          |
|------------------|----------------------------------|
| MongoDB shell 4.2| SSPL 1.0                         |
| Qt 5             | LGPL-3.0 / GPL-2.0               |
| OpenSSL 1.1      | Apache License 2.0               |
| libssh2 1.11     | BSD 3-Clause                     |
| SpiderMonkey 60  | MPL 2.0                          |
| boost, pcre, yaml-cpp, fmt, abseil | permissive (BSD/MIT/Apache) |

Romo is a derivative work of Robomongo / Robo 3T by Studio 3T and the Robomongo
contributors — thank you. The "Robo 3T" and "Studio 3T" names and logos are
trademarks of their respective owners and are not used by this project.
