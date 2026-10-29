# AGENTS.md

Lepton is a Bedrock port of the *behaviour* of the Java mod **Quark 4.1-488** (Vazkii),
written as a LeviLamina native mod. It targets MC **1.26.51** / LeviLamina **26.51.7**, builds with
xmake + clang-cl on Windows x64, and runs on **both** the dedicated server (BDS) and the client
(local worlds through LeviLauncher). One code base, two builds (`--target_type=server|client`).

The behaviour reference is `docs/SPEC.md`. Write it (blocks, items, mechanics, every screen, config,
commands, strings) before the first feature unit, and read it before working on a unit.

Policy: never copy code or assets from the Java Quark repository (behaviour reference only;
it is licensed CC BY-NC-SA 3.0). Textures are original pixel art drawn by `tools/textures/*.ps1`. English UI strings
should match the wording of Quark (they are behaviour), written into our own lang files.

## Layout

| Path | What it is |
| ---- | ---------- |
| `src/core/` | Pure C++ (no `mc/`/`ll/` includes): `Format.h` (translation formatting), `Glob.h`, `GlobalPos.h`. Static lib `lp-core`, unit-tested in `tests/`. |
| `src/game/` | Game adapters: `WorldDb` (LevelDB JSON + world lifecycle), `Blocks` (states, placement, positions). |
| `src/client/` | Client side (client build only, files wrapped in `#ifdef LP_CLIENT`): `Client.h` (client instance, local player, `ctr`). |
| `src/mod/` | Entry (`Main.cpp`), `Log.h`, `Module.h`, `Config.h`, `ModConfig.h`, `I18n.h`, `Commands.h`, `Registry.h`. |
| `src/selftest/` | In-game test framework (`SelfTest.h`) and the foundation tests. |
| `src/pack/` | `PackDeployer` (BDS: puts the resource pack on the world). |
| `src/feature/<unit>/` | Feature units (gameplay, server side; may contain `#ifdef LP_CLIENT` parts). |
| `assets/lang/<locale>/<unit>.json` | Mod translations (server + client mod text), merged into `lang/<locale>.json`. |
| `assets/pack/bp/` | Behavior pack: `blocks/`, `items/`, `recipes/`, `loot_tables/`, `manifest.json`. |
| `assets/pack/rp/` | Resource pack: `textures/`, `models/`, fragments `item_texture.d/`, `terrain_texture.d/`, `blocks.d/`, `sounds.d/`, `texts.d/<locale>/*.lang`. |
| `tools/build.ps1` | Builds server and/or client into `bin/<target>/Lepton/`. |
| `tools/xmake/assemble.lua` | Post-build: copies binaries, merges lang + pack fragments. |
| `tools/textures/lib.ps1` | Pixel-art helpers (`Save-PixelArt`, `Merge-Rows`, `Shade-Color`). |
| `tools/e2e/` | Real-game harnesses (server + client); see `tools/e2e/README.md`. |
| `docs/SPEC.md`, `docs/spec/<unit>.md` | Behaviour spec: module inventory, conventions, config keys, work plan (§9); per-unit detail in `docs/spec/` (template in `docs/spec/README.md`). |
| `src/game/Items.h` | `makeItem(id, count, aux)`, `itemId`, `isEmpty`, `canMerge`, `itemToSnbt` / `itemFromSnbt`. |
| `src/game/Inventory.h` | `lp::game::inv`: slot ranges (`kHotbar`, `kPlayerMain`, `kPlayerAll`), `insert`, `roomFor`, `transferSlot/All`, `count`, `remove`, `compact`, `sort`, `snapshot/restore`, `sync`. |
| `src/game/Forms.h` | `lp::game::forms`: `SimpleForm`, `ModalForm`, `CustomForm` (+ `Response`), `closeFor`; `forms::testing::click/submit/close` answer forms in selftests. |
| `src/game/Entities.h` | `lp::game::entities`: `spawn`, `find(uniqueId)`, `idOf`, `typeOf`, `inBox`, `ofType`, `triggerEvent`, `despawn`. |
| `src/game/VirtualContainer.h` | `lp::game::vc`: container contents in the world DB opened in the vanilla chest/hopper/dispenser screen (`open`, `items`, `setItems`, `takeAll`, `dropContents`, `liveContainer`, `bindBlock`). |
| `src/game/BlockStates.h`, `src/game/Shapes.h` | Typed block states (`blockstates::get/with/of`) and the native behaviour of generated shapes (`lp::game::shapes`: stairs corners, slab merging, connections, doors, gates, buttons, ...; state and tag names in the header and `tools/pack/README.md`). |
| `src/net/Net.h` | Companion protocol (custom LeviLamina packets): `hasClientMod`, `onServer/onClient`, `sendToClient/sendToServer/broadcast`, `net::testing`. |
| `src/client/ui/Ui.h` | Client UI toolkit (client build): `DrawContext`, HUD hooks, screen overlays (buttons, text fields, greyed slots), tooltip lines, keybinds, custom screens. Demo: `UiDemo.cpp`. |
| `tools/pack/lib.ps1` | Pack generators `New-ShapeSet` / `New-WoodSet` (+ single blocks, recipes, loot, fragments); see `tools/pack/README.md`. Units run them from `tools/pack/<unit>.ps1` and commit the output. |
| `assets/pack/rp/ui/`, `ui_defs.d/<unit>.json` | JSON-UI files of the resource pack; each fragment is a JSON array of `ui/*.json` paths merged into `ui/_ui_defs.json`. |
| `assets/pack/bp/entities/`, `assets/pack/rp/entity/` | Behaviour / client entity definitions (`lepton:virtual_container` helper, `lepton:u0_probe_entity` test entity). |

## Work as a feature unit (no shared-file edits)

Each feature unit owns its own files and registers itself. **Do not edit** `src/mod/Main.cpp`,
existing contracts or other units' files. Extend contracts compatibly (add, never change/remove)
and only when your unit really needs it; call such additions out in the PR.

Files a unit typically adds (`<unit>` = a short lowercase name):
- `src/feature/<unit>/*.cpp|h`, namespace `lp::feature::<unit>`; client parts in `src/client/<unit>/`.
- `assets/lang/en_US/<unit>.json` + `assets/lang/zh_CN/<unit>.json` (both required).
- BP: `assets/pack/bp/blocks/<id>.json`, `items/<id>.json`, `recipes/<id>.json`, `loot_tables/blocks/<id>.json`.
- RP: `textures/blocks|items/*.png` (drawn by `tools/textures/<unit>.ps1`), `models/blocks/*.geo.json`,
  fragments `terrain_texture.d/<unit>.json`, `item_texture.d/<unit>.json`, `blocks.d/<unit>.json`,
  `sounds.d/<unit>.json`, `texts.d/{en_US,zh_CN}/<unit>.lang` (block / item names).
- Selftests in the unit's .cpp (`LP_REGISTER_SELFTEST("<unit>.<case>", ...)`), e2e scenarios in
  `tools/e2e/{server,client}/scenarios/<unit>.ps1`.

Duplicate keys in any merged fragment (lang JSON, item_texture, terrain_texture, blocks, texts,
sounds) fail the build. A translation key belongs to the unit that owns the feature; other units
reference it with `tr(key, "English fallback")` without defining it.

### Registration macros

| Macro | Header | Purpose |
| ----- | ------ | ------- |
| `LP_REGISTER_MODULE(T)` / `LP_REGISTER_MODULE_ORDERED(T, order)` | `mod/Module.h` | `T` has `install()`/`uninstall()` (+ optional `load()`, + optional `static T& get()`). |
| `LP_REGISTER_CONFIG("section", T)` | `mod/Config.h` | Reflectable struct with `int version`; file `config/<section>.json`; `lp::config::get<T>()`. One section per unit. |
| `LP_REGISTER_SUBCOMMAND(fn)` | `mod/Commands.h` | Adds overloads to `/quark` (alias `/lepton`). `commands::playerOf`, `commands::isAdmin`. |
| `LP_REGISTER_SELFTEST("unit.case", fn)` | `selftest/SelfTest.h` | `fn(Context&)` or `ll::coro::CoroTask<>(Context&)`. |
| `LP_REGISTER_MESSAGE(T)` | `net/Net.h` | Companion-protocol message: reflectable struct with `static constexpr std::string_view kId = "<unit>:<name>"`, external linkage. Handlers: `net::onServer<T>` / `net::onClient<T>` in `install()`. |
| `LP_REGISTER_KEYBIND("name", Key::X, onDown[, onUp])` | `client/ui/Ui.h` | Client keybind `key.lepton.<name>` (rebindable in Controls; default from `config/keybinds.json`). Add the `key.lepton.<name>` name to your `texts.d` lang. |
| `LP_REGISTER_HUD(fn)` | `client/ui/Ui.h` | `fn(DrawContext&)` draws on the HUD every frame. |
| `LP_REGISTER_SCREEN_OVERLAY("glob", fn)` | `client/ui/Ui.h` | `fn(Overlay&, ScreenInfo const&)` adds widgets to screens whose name matches (`inventory_screen`, `*chest_screen`, ...). |
| `LP_REGISTER_TOOLTIP(fn)` | `client/ui/Ui.h` | `fn(ItemStackBase const&, std::vector<std::string>& lines)` appends tooltip lines. |

Registrations live in .cpp files linked directly into the DLL, so they are never stripped.

## Contracts in short

- **Ids**: `lepton:<path>`, with the paths Quark uses where they exist.
- **World data** (`game/WorldDb.h`): JSON in the world's LevelDB under `lp:<unit>:<...>` keys.
  `db::onWorldReady/onWorldSave/onWorldClosed` for the world lifecycle (the client loads many
  worlds in one process). Never keep `Player*` or block pointers across ticks.
- **Strings**: `lp::tr("key", "English fallback", args...)` / `lp::trFor(player, ...)`, client
  `lp::client::ctr(...)`, `{}`/`{0}` placeholders. Prefer the key names of Quark for strings
  that exist there; `lp.<unit>.*` for Lepton-only text. Block / item names come from RP `texts.d`.
- **Logging**: `lp::log()` (`mod/Log.h`). No per-tick logging. Log one line per user-visible action
  that e2e scenarios can wait for.
- **Selftests** (`selftest/SelfTest.h`): `ctx.player()` (SimulatedPlayer), `ctx.spawnPlayer()`,
  `ctx.testPos(dx)` (distinct `dx` per test; unit U<n> owns dx 100n..100n+99; the run keeps the
  area loaded with a ticking area), `ctx.setBlock(pos, id, states)`, `ctx.cleanupBlock`, `ctx.give`,
  `co_await ctx.ticks(n)`, `co_await ctx.ensureLoaded(pos)`, `ctx.check/require/fail/info`.
  `/quark selftest` (no glob) and the server and client scenarios `full` run every unit's tests
  together: keep them passing there. A test that waits for a client scenario's input (real clicks)
  must skip that part when nobody acts (the client `full` run has a real player who never clicks).
- **Companion protocol** (`net/Net.h`): LeviLamina runtime packets (one per direction) carrying
  `(kId, JSON)`. The client mod says `lp:hello` after joining; the server answers `lp:welcome`.
  The server sends Lepton messages **only** to `net::hasClientMod(player)` players (`sendToClient`
  returns false otherwise), so vanilla clients never see unknown packets; give them a fallback
  (`game/Forms` or `game/VirtualContainer`). The client checks `net::serverHasMod()`. Server
  handlers get the sending `Player&`: validate everything (a client may lie). Selftests mark a
  SimulatedPlayer as a mod client with `net::testing::setClientMod`, read what was sent with
  `takeOutbox`, and inject client messages with `receiveFromClient`. `/quark net` lists who has the mod.
- **Vanilla-client fallbacks**: `game/Forms.h` (`SimpleForm`/`ModalForm`/`CustomForm`, per-button
  callbacks, `onClose`; texts are sent as given, translate with `trFor`). Containers: `game/VirtualContainer.h`
  (`vc::bindBlock("lepton:crate", spec)` in `install()` makes using the block open its inventory and
  breaking it drop the contents; contents live under `lp:vc:<dim>:<x>:<y>:<z>`). The helper stands in the block if it is not solid, else on top of it, else at the viewer's feet (inside a solid block it loses its contents); screens open a few ticks after the helper spawns. The client gets the full contents right after the screen opens. `Screen::Dispenser` (3x3) and chests of 28..54 slots (double chest screen) are served by the helper's chest model on the server and a dispenser / a pair of chests shown to that client only at the container's position (the second chest east of it), replaced by the real blocks when it closes. A viewer that uses a block or asks for another container no longer views its screen (the client dropped it): the server closes it. Every client gets
  the vanilla screen, served by a hidden helper entity, so item moves are server-authoritative.
- **Shapes** (`game/Shapes.h`): blocks generated by `New-ShapeSet` / `New-WoodSet` get vanilla-like
  native behaviour from their tags (`lepton:stairs`, `lepton:slab`, ...) automatically; don't
  re-implement it. Vertical slabs: `lepton:type` (north|south|west|east|double).
- **Client UI** (`client/ui/Ui.h`, client build): immediate-mode drawing in UI units on top of the
  game's screens; screen names are the JSON-UI root element names without the namespace
  (`inventory_screen` for the game's `crafting.inventory_screen`; `ScreenInfo::qualifiedName` has
  both; the 20th frame of a screen logs its control tree at debug level: use it to find slot
  collections and positions). Screens animate in: slot rects follow the animation every frame;
  wait for `ScreenInfo::settled` before one-shot work with positions. Only the top screen's
  widgets take input; key and mouse releases always reach the game. Text
  fields take key presses (letters, digits, space, `-`, `.`). Custom screens: subclass
  `ui::CustomScreen`, `ui::openScreen(...)`; Esc closes them. The demo (`config/ui.json`
  `"demo": true`, client scenario `ui`) shows every feature.
- **Items** need `game::makeItem` (`game/Items.h`; counts are capped at the max stack size).
  Entities: `game/Entities.h`; store `entities::idOf(actor)`, never `Actor*`.

## Build, test, run

In a fresh pwsh refresh PATH first (the build script does this and adds LLVM if needed):
`$env:Path=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')`

```powershell
pwsh tools/build.ps1 -Target all -Tests     # bin/server/Lepton + bin/client/Lepton + gtest
pwsh tools/build.ps1 -Target server -Tests  # one platform + unit tests
xmake build -y lp-tests; xmake run lp-tests
pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario smoke       # BDS (4 slots, waits for a free one)
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario smoke       # headless client via LeviHeadlessInput
```

- Builds are throttled machine-wide: `tools/build.ps1` waits for one of `$env:LP_BUILD_SLOTS` (default 2)
  build slots and compiles with `-Jobs` (`$env:LP_BUILD_JOBS`, default half the logical processors).
  Always build through `tools/build.ps1`, never with a bare `xmake build` (it would bypass the slots).
- `git config --global core.longpaths true` is required (long LeviLamina header paths).
- `clang-cl` must be on PATH (`C:\Program Files\LLVM\bin`).
- `xmake-requires.lock` is committed; it holds entries for both target types.
- In game: `/quark selftest ["glob"]` (op or server console). Quote globs: `quark selftest "<unit>.*"`.
  Output lines: `SELFTEST PASS <name>`, `SELFTEST FAIL <name>: <reason>`, `SELFTEST DONE pass=<N> fail=<M>`.
- Log contract: `Lepton enabled` on enable; `Lepton world ready` when a world is available.
- E2E client runs: set `$env:LEVI_E2E_SLOTS='1,2,3,4'` to take the first free game slot. Locks live in
  `%APPDATA%\levilauncher.exe\` (`bei-e2e.lock` for slot 1, `hiN-e2e.lock` for clones); the harness
  waits for them. Headless input (LeviHeadlessInput) is the default: the game runs in the
  background and never takes the real mouse/keyboard.
- Worktrees under the main checkout: run xmake with `-P .` (as `tools/build.ps1` does), otherwise xmake
  picks the parent checkout's project and builds there. If a build rewrites `xmake-requires.lock`
  (repo commit bumps only), revert it: `git checkout xmake-requires.lock`.
- Client remote modes download the RP from the BDS host; the harness drops the game's cached copy of
  Lepton's RP before every launch (same UUID/version in every dev build), so remote runs test this build's assets.
- Client e2e: `full` (all selftests + handshake), `ui` (UI toolkit demo, screenshots), `net` (run with
  `-Mode local`, `remote-mod` and `remote-vanilla`: handshake, container screen, form fallback),
  `shapes` (generated shape gallery).

Deploying by hand: copy `bin/server/Lepton` to `<BDS>/plugins/`, or `bin/client/Lepton` to
`%APPDATA%\levilauncher.exe\versions\1.26.51.01\mods\`.

## Bedrock / LeviLamina rules (learned the hard way)

- Headers: `%LOCALAPPDATA%\.xmake\packages\l\levilamina\26.51.7\<hash>\include\{ll,mc}` (one hash per
  target type). Check them before guessing a name or signature.
- Callable: `MCAPI`, `MCFOLD`, `MCTAPI`, unmarked `virtual`. **Never** `MCNAPI`. `ll::TypedStorage`
  members are fields (`.get()`, `*`, `->`). Packets without an MCAPI constructor are created with
  `MinecraftPackets::createPacket(MinecraftPacketIds::X)` + `static_cast`; send with `pkt.sendTo(player)`.
- Hook virtuals via `&Cls::$fn` with `LL_TYPE_INSTANCE_HOOK`; `hook()` in `install()`, `unhook()` in `uninstall()`.
- In `ServerNetworkHandler::$handle` hooks `this` is the `NetEventCallback` base subobject: use
  `thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId)`.
- **No `dynamic_cast` on game types** (no usable RTTI). A Player that is not `isClientSide()` is a `ServerPlayer`.
- Types used as command parameters must have external linkage (not in an anonymous namespace).
- `ItemStack` has no `(name, count, aux)` constructor: use `game::makeItem(id, count, aux)`.
- BDS enables mods **after** "Server started" (the world already exists at `install()`); the client
  enables mods at game start (no world yet) and fires `ServerStartedEvent` for every local world.
  Write modules that work in both orders (`game::db::onWorldReady`).
- `ll::service::getLevel()` on the client is the integrated server's level (null outside a world).
- Behavior packs in `mods/<name>/behavior_packs/` are added to every world by LeviLamina; resource
  packs are added to the client's global stack. On BDS `PackDeployer` adds the RP to the world.
- Custom blocks cannot have block entities: keep per-block state in block states + the world DB.
- A cancelled `PlayerInteractBlockEvent` rolls back block changes made during it: defer world edits
  to the next tick (`ll::coro` / `ServerLevelTickEvent`).
- Don't keep `Player*` across ticks; store `getOrCreateUniqueID()` / the UUID and look it up again.
- `PlayerInteractBlockEvent::block()` may be empty: read the block from the region at `blockPos()`.
  `PlayerPlacedBlockEvent` fires before the block exists: fix placements up on the next tick.
- `SimulatedPlayer::simulateUseItemOnBlock` needs a non-empty item (an empty hand never reaches
  `useItemOn`), and a second use within a few ticks is ignored: wait ~10 ticks between uses.
- Reflected types (config structs, net messages, command parameters) need external linkage.
- `Block::tryGetFromRegistry(name, states)` silently returns the default state for unknown states:
  use `game::blockstates::with/of`.
- `NetEventCallback` has no `_getServerPlayer` in 26.51.7: find the player by
  `Player::getNetworkIdentifier()` (see `src/net/Net.cpp`).
- Block geometry (`models/blocks/*.geo.json`) takes plain numbers only. One string number (e.g.
  `"rotation": [0, "-45", 0]`) makes the client drop **every custom block of that 16x16x16
  sub-chunk** from its mesh: vanilla blocks still render, the server still has the blocks, and
  nothing is logged. In PowerShell, `& $block -45` passes the string `'-45'` (command mode): type
  the parameter (`param([double]$rot)`) or write `(-45)`. `Write-PackJson` and the build (`assemble.lua`) reject such geometry; the
  client scenario `renderprobe` puts one block per model kind in its own sub-chunk next to a canary.
  (Per-cube and per-bone `rotation` with numbers both render fine.)
- **Comparator output of a custom block**: data-driven blocks can't declare one, and
  `BlockType::$hasComparatorSignal` / `$getComparatorSignal` are `MCFOLD` (shared with unrelated
  functions), so don't hook them. Give that one block type its own vtable copy instead (`patchVirtual`
  in `src/feature/redstone/Common.cpp`): take the slot from the vcall thunk of
  `&BlockType::hasComparatorSignal` (`disp / 8`), copy the vtable including the RTTI entry at -1
  (`VirtualQuery` to stay in readable memory), replace the slot with a free function taking
  `(BlockType const* self, ...)`, point the type's vptr at the copy. Do it per world
  (`db::onWorldReady`; the client re-creates block types), forget it on world close, restore on
  uninstall. Works for any other `BlockType` virtual of one custom block.
- A data-driven `minecraft:redstone_producer` keeps the output faces of the permutation it was first
  created for. When a block changes its faces, remove its circuit component (`CircuitSceneGraph::remove`)
  and call `BlockRedstoneProducerComponent::setupRedstoneComponent` again.
- A looping `minecraft:tick` doesn't start for blocks placed with `BlockSource::setBlock` (commands,
  world gen, natively placed): queue the first tick with `addToTickingQueue` and re-queue it in a
  `Block::queuedTick` hook.
- `BlockMovableComponent::mMovementType` of a block's permutations can be changed at runtime to
  change what pistons move.
- The server can't close vanilla block screens (anvil, smithing table) on the client: mod clients
  close them when the server sends a net message (U13).
- **Never call a static / free game function that returns `ActorUniqueID` (or another type that is an
  aggregate in LeviLamina but has constructors in the game) by value.** The game returns it through
  a hidden result pointer, clang-cl expects it in RAX, so the game writes the result over the first
  argument (`MapItem::getMapId` overwrote the map tag's vtable and crashed the client later). Read
  the value another way (for maps: `map_uuid` from the item's tag).
- A custom item with `minecraft:dyeable` has an invisible UI icon unless `minecraft:icon` also has a
  grey `dyed` texture (tinted with the colour).
- Block states have at most 16 values each (and a block at most 16 bits of states in total).
- Record items (`minecraft:record`) only accept vanilla sound events.
- Bedrock rejects a resource pack animation file whose animation has an empty `bones` object: leave
  the key out.
- `NetherGenerator` and `TheEndGenerator` share `$decorateWorldGenPostProcess`: a hook on it must check
  the dimension itself.
- Calling `getOffers()` inside a `_generateTrades` hook recurses into the hook.
- A vanilla flower pot holding a custom plant that has `minecraft:flower_pottable` but no
  `minecraft:embedded_visual` crashes the client while it builds the pot's mesh (U7's saplings). With
  an `embedded_visual` (U29's chorus weeds / twist) the potted plant renders fine. Otherwise swap the
  pot for a custom potted block (U7 `Potted.cpp`, U8, U28).
- The dedicated server builds some texts with its own I18n, which only knows the vanilla strings
  (e.g. the `/give` feedback): `PackDeployer` appends the pack's `texts/<lang>.lang` to it at install.
  Pass translation keys to clients wherever possible instead of server-translated names.
- A container screen only opens on the client after the client asked for it, and the game drops a
  screen that opens while another one is still closing (wait ~0.5 s after a close). The client shows
  an entity container's contents only after the server sends them: resend right after opening and a
  few ticks later (U14, `VirtualContainer`).

## Conventions

- C++20, `.clang-format` (LLVM-based, 4 spaces, 120 columns). Namespaces follow directories
  (`lp::core`, `lp::game`, `lp::feature::<unit>`, `lp::client::...`).
- English in code and comments; user-facing text through `tr`/`trFor`/`ctr` + lang fragments (en_US and zh_CN).
- Every `src/core` change comes with gtest coverage in `tests/`; every feature comes with selftests
  and an e2e scenario.
- Commit in small, buildable steps. Both platforms must build (`pwsh tools/build.ps1 -Target all`).
- Commit / PR titles in the imperative mood, as in [CONTRIBUTING.md](CONTRIBUTING.md) ("Add the biome search cooldown"),
  PR into `main`. Work on a unit of the plan may prefix its title with the unit id (`U<n>: <summary>`).

## Unit checklist (wave 1)

1. Read `docs/SPEC.md` (your row in §9, your modules in §2.1, your config keys in §5.1) and the
   Quark source of your modules (behaviour only). Write `docs/spec/<unit>.md` first (template:
   `docs/spec/README.md`).
2. Own only your files (see "Work as a feature unit"): `src/feature/<unit>/`, `src/client/<unit>/`,
   `assets/lang/{en_US,zh_CN}/<unit>.json`, pack files and `<unit>` fragments, `tools/pack/<unit>.ps1`,
   `tools/textures/<unit>.ps1`, `tools/e2e/{server,client}/scenarios/<unit>*.ps1`.
3. Config: `LP_REGISTER_CONFIG("<unit>", T)` with one `enable<Module>` per module and Quark's options
   and defaults.
4. Blocks: generate shapes / wood sets with `tools/pack/lib.ps1`, commit the output; shared native
   behaviour comes from `game/Shapes`. Containers: `game/VirtualContainer`. UI: `client/ui` for mod
   clients, `game/Forms` / vanilla screens for vanilla clients (decide with `net::hasClientMod`).
5. Selftests named `<unit>.*` in your dx range (U<n>: 100n..100n+99), passing inside the server
   `full` scenario; an e2e scenario per unit (client scenarios with screenshots for anything visual).
6. `pwsh tools/build.ps1 -Target all -Tests` and `tools/e2e/server/run.ps1 -Scenario full` pass.
7. **Do not edit** `CHANGELOG.md`, `README.md`, `docs/SPEC.md` or `AGENTS.md`: put the changelog
   lines, README lines and any spec/contract change you need in your PR body (sections "Changelog",
   "README", "Spec changes"); the coordinator merges them.
8. PR title `U<n>: <imperative summary>` into `main`.

## Out of scope

See [docs/SPEC.md §8](docs/SPEC.md#8-not-ported) ("Not ported"): Quark features with no Bedrock
equivalent (config GUI, mod-compat and Java-only modules after U27's review).
