# Lepton

[![Build](https://github.com/LiteLDev/Lepton/actions/workflows/build.yml/badge.svg)](https://github.com/LiteLDev/Lepton/actions/workflows/build.yml)
[![License](https://img.shields.io/github/license/LiteLDev/Lepton)](COPYING.LESSER)

Lots of small things that make vanilla better, each one a module you can turn off. Lepton brings Vazkii's Java mod [Quark](https://modrinth.com/mod/quark) to Bedrock, for dedicated servers and single-player worlds.

> [!NOTE]
> Lepton is in early development. There is no release yet.

## Install

You need Minecraft Bedrock Edition **1.26.51** for Windows (x64) with LeviLamina **26.51.x** (on a client, installed through [LeviLauncher](https://github.com/LiteLDev/LeviLauncher)).

With [lip](https://github.com/LiteLDev/lip):

```sh
lip install github.com/LiteLDev/Lepton           # dedicated server
lip install github.com/LiteLDev/Lepton#client    # client
```

Or download the zip from the [latest release](https://github.com/LiteLDev/Lepton/releases/latest) and extract it next to `LeviLamina/`:

| Where | Zip | Folder |
| --- | --- | --- |
| Dedicated server | `lepton-server-windows-x64.zip` | `plugins/Lepton/` |
| Client (LeviLauncher) | `lepton-client-windows-x64.zip` | `mods/Lepton/` |

## Commands

| Command | Who | What it does |
| --- | --- | --- |
| `/quark reload` | Operators, console | Reloads the configuration and translations. |
| `/quark selftest ["glob"]` | Operators, console | Runs the in-game selftests. |

`/lepton` is an alias of `/quark`.

## Configuration

Settings live in `config/*.json` inside the mod folder (`plugins/Lepton/` or `mods/Lepton/`). Each file is created with defaults on first start.

| File | Setting | Default | Meaning |
| --- | --- | --- | --- |
| `mod.json` | `language` | `en_US` | Language of server messages when a player's own language has no translation. |
| | `usePlayerLocale` | `true` | Use each player's game language when a translation exists. |
| | `requireResourcePack` | `true` | Dedicated server: players must accept the resource pack to join. |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). [AGENTS.md](AGENTS.md) is the developer guide.

## Credits

Lepton reproduces the behaviour of [Quark](https://github.com/VazkiiMods/Quark) by Vazkii. It copies no code or assets from it.

## License

[LGPL-3.0-or-later](COPYING.LESSER)
