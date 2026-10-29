# U19 emotes: player emotes on the model, and petting dogs

## 1. Modules covered

### Emotes (Quark `tweaks/EmotesModule`)
Players play short animations ("emotes") on their own player model, seen by everyone. Quark ships
18 emotes: 12 default ones (`no`, `yes`, `wave`, `salute`, `cheer`, `clap`, `think`, `point`,
`shrug`, `headbang`, `weep`, `facepalm`) and 6 Patreon emotes (`dance`, `tpose`, `dab`: tier 1;
`jet`, `exorcist`, `zombie`: tier 2) reserved for Vazkii's patrons. The chat screen gets an
"Emotes" button that toggles a grid of emote buttons; every emote also has an (unbound) keybind.
While the local player emotes, its icon and name show above the crosshair. Emotes are scripts
(`.emote` files: parts used, tweens on head/body/arms/legs/whole model, sounds); custom emotes
are loaded from `config/quark_emotes/<name>.emote`.

### PatTheDogs (Quark `tweaks/PatTheDogsModule`)
Sneak and use an empty main hand on a tamed wolf to pet it: a heart particle and a whine. With
config, other mobs can be pet too (with their own sound). With `dogsWantLove` > 0, tamed wolves
that were not pet for that many ticks hop at their owner and follow them around until pet.

## 2. Blocks and items

None. Resource-pack content:

| File | What |
|---|---|
| `assets/pack/rp/animations/lepton_emotes.animation.json` | The 18 emotes baked into Bedrock animations `animation.lepton.emote.<id>` (+ `animation.lepton.emote.stop`). Generated from the emote scripts by the unit tests (`tests/emotes/`, `LP_UPDATE_EMOTE_ANIMATIONS=1`); a test fails when it is stale. |
| `assets/pack/rp/textures/lepton/emotes/<id>.png` | 16x16 icons (18 emotes, `custom`, `patreon_t1`, `patreon_t2`), original art by `tools/textures/emotes.ps1`, used by the vanilla-client form. The same pixels are compiled into the client mod (`src/client/emotes/EmoteIcons.inc`). |
| client only: `mods/Lepton/resource_packs/Lepton_RP/animations/lepton_custom_emotes.animation.json` | Written by the client mod at game start from the custom emote files. |

## 3. Mechanics

### Emotes
- **Request.** Mod client: a chat-screen button or a keybind sends `emotes:request` (Quark
  `RequestEmoteMessage`). Vanilla client: `/quark emote <name>` or the form of `/quark emote`.
- **Server validation** (Quark validates nothing on the server; the receiving clients drop
  unknown or too-high-tier emotes): `enableEmotes`; a built-in default emote must be in
  `enabledEmotes`; a patreon emote needs the player's tier >= the emote's tier; `custom:<name>`
  (name of letters, digits, `_`, `-`, `.`, max 64) is relayed as is (Quark relays anything).
- **Rate limit** (Lepton): a player's requests (`emotes:request`, `/quark emote <name>`, the form)
  are accepted at most every 5 ticks, so a client cannot flood the other clients (Quark has no
  limit); the command then answers "Wait a moment before the next emote." (`lp.emotes.throttled`).
- **Endless emotes**: a negative `repeat`/`yoyo` count repeats forever (the tween engine's
  INFINITY); it plays until the player swings or is hurt (the baked animation is capped at an hour).
- **Tiers** (Quark `ContributorRewardHandler`: the patron list downloaded from Quark's GitHub,
  by Java account name): `emoteTiers` maps a lower-case player name or XUID to a tier; everyone
  else has `defaultEmoteTier` (0, like a non-patron).
- **Playing.** The server sends an `AnimateEntityPacket` (the packet of `/playanimation`) with
  `animation.lepton.emote.<id>` on controller `controller.animation.lepton.emote` to every client
  that sees the player (vanilla clients too: the animations are in the resource pack), with the
  stop expression `variable.attack_time > 0.0 || query.hurt_time > 0.0` (Quark: an emote ends when
  the player swings or is hurt). A new emote replaces the running one (same controller; Quark
  resets the model first). It also broadcasts `emotes:play` (Quark `DoEmoteMessage`: emote, player,
  tier) to every mod client for the HUD and sounds. Log: `emotes: <player> plays <id>`.
- **Animation semantics** (the emote script language, `src/feature/emotes/EmoteScript.h`):
  `use <part>` (head, body, right_arm, left_arm, right_leg, left_leg, model), `unit <factor>`
  (multiplies later times), `tier <n>`, `name <text>`, `animation sequence|parallel` (once),
  `section sequence|parallel` ... `end`, `move <part>_<x|y|z> <ms> <radians> [delay <ms>]
  [yoyo <n> <ms>] [repeat <n> <ms>] [ease <equation>]`, `reset <part|all> <all|rotation|offset>
  <ms>` (tweens the used parts' channels to 0; Quark's channel range, including its "offset" quirk),
  `pause <ms>`, `yoyo|repeat <n> <ms>` (on the current section), `sound ...` (below). Errors are
  reported like Quark (`[Custom Emotes] Error loading line N of emote X` + the message) and the
  emote falls back to an empty animation. Timing follows the Universal Tween Engine: default
  easing Quad in-out, a tween captures its start value when it starts, yoyo iterations play back,
  repeat restarts from the first start value, repeat delays hold the iteration's end value,
  sequences chain full durations (delay + iterations), parallels take the longest. Every easing
  equation of the tween engine is available (`linear`/`none`, `in|out|in_out` + `quad`, `cubic`,
  `quart`, `quint`, `circ`, `sine`, `expo`, `back`, `bounce`, `elastic`).
- **Durations** (ms): no 1400, yes 1400, wave 1600, salute 2950, cheer 2400, clap 2000, think 3900,
  point 1800, shrug 800, headbang 4600, weep 8250, facepalm 3700, dance 3800, tpose 600800,
  dab 2800, jet 9900, exorcist 7500, zombie 9700 (= Quark's timeline durations).
- **Baking.** The script is sampled every 10 ms and simplified (a keyframe is dropped when the
  straight line through its neighbours is within 0.2°). Bedrock animations add to the pose while
  Quark sets the model parts, so each channel value `a * start + b` (start = the pose when the
  emote began) is written as `b - (1 - a) * this` (degrees). Bone mapping: head, body, rightarm,
  leftarm, rightleg, leftleg; `model` rotates the `root` bone around the middle of the player
  (half its height, 14.4 px) like Quark's pose-stack rotation. Rotation signs equal Java's
  model-part signs (Bedrock's humanoid animations use the same conventions).
- **Visibility.** `blend_weight` hides the emote in first person and outside Quark's "standing"
  pose (sneaking, swimming, sleeping, gliding): Quark keeps emoting but stops showing it then.
- **HUD** (mod client): while the local player emotes, its icon (32x32) is drawn at
  (width/2 - 16, height/2 - 60) and its name centred 34 units below, for the emote's length or
  until the player swings or is hurt (Quark `EmoteBase.isDone`; the emote's sounds stop too).
- **Sounds** (mod clients, custom emotes): `sound instant|continuous section|emote <sound>
  [volume (max 1.5)] [pitch]` plays when the statement before it in its section completes (or
  when the section starts); `section` sounds end when their section completes; `sound stop` ends
  all; a new `animation` start ends all; continuous sounds are replayed every second until they
  end (the client sound interface usable from the mod has no loop call). Sounds are played at the player's
  position by each mod client that runs the emote.
- **Custom emotes** (client mod): every name in `customEmotes` is read from
  `config/quark_emotes/<name>.emote` (the folder is created) at game start, compiled, and baked
  into the client's resource pack (`lepton_custom_emotes.animation.json`). Their buttons use the
  `custom` icon and their `name` line (or the file name). As in Quark only clients that have the
  file see a custom emote; a custom emote whose `tier` is above the local tier is hidden.
  `customEmoteDebug`: the file is read again every time the emote is triggered (timing, sounds,
  HUD), the baked animation is refreshed at the next game start.

### PatTheDogs
Hook: `Player::interact(Actor&, Vec3)` on the server (Quark: `PlayerInteractEvent.EntityInteract`).
Only when the player sneaks with an empty main hand:
- **Wolves** (`minecraft:wolf`, and `lepton:foxhound` which is a wolf in Quark): the interaction
  is always cancelled (no sit toggle). If the wolf is tamed and was last pet more than 20 ticks
  ago: one heart particle at the wolf's position + 0.5 y, `mob.wolf.whine` at volume 1, pitch
  0.5..1.0, the pet time is recorded (only when `dogsWantLove` > 0). Petting a foxhound sets the
  player on fire for 1 s unless they are in water, have Fire Resistance or are in creative.
- **Other living mobs** when `petAllMobs`, or the target is a player, or its id is in
  `pettableAllowlist`; never if its id is in `pettableDenylist`: a heart at the eye position + 0.5,
  the mob's sound with pitch `centre ± 0.25`, interaction cancelled. Sounds: axolotl
  `mob.axolotl.splash`; cat, ocelot `mob.cat.purreow`; chicken `mob.chicken.say`; cow, mooshroom
  `mob.cow.say` (centre 1.2); horse, donkey, mule, skeleton/zombie horse, llama, trader llama,
  camel `mob.horse.idle`; cod, salmon, pufferfish, tropical fish, tadpole `mob.fish.step`; fox
  `mob.fox.sleep`; squid `mob.squid.ink_squirt`, glow squid `mob.glow_squid.ink_squirt` (centre
  1.2); parrot `mob.parrot.idle`; pig `mob.pig.say`; rabbit `mob.rabbit.idle`; sheep
  `mob.sheep.say`; strider `mob.strider.happy`; turtle `mob.turtle.ambient`. Other mobs: pettable
  without sound when `petAllMobs` or allowlisted. Players: pettable when `minecraft:player` is
  allowlisted (no sound). Log: `pat: <player> pets a <type>`.
- **Dogs want love** (`dogsWantLove` > 0, Quark `WantLoveGoal` + `NuzzleGoal`), every tick for
  tamed wolves whose owner is online in the same dimension and that were not pet for more than
  `dogsWantLove` ticks: when 2..4 blocks from the owner (distance² 4..16), on the ground, 1 in 5
  chance: a hop (velocity += towards the owner 0.4 + 0.2 × current horizontal velocity, +0.2 up),
  until it lands; otherwise, unless the dog sits or the owner is a spectator, it paths to the owner
  at speed 0.5 (path rebuilt every 10 ticks) and whines (`mob.wolf.whine`, pitch 0.5..1.0) within
  distance² 2, every 80..119 ticks (first after 10). Taming records the pet time (Quark:
  AnimalTameEvent). Pet times are stored per wolf in the world DB (`lp:emotes:pet_times`, game
  tick of the last pet; Quark: the wolf's `quark:PetTime` tag).

## 4. Screens

- **Chat screen buttons** (mod client, `chat_screen` overlay): an "Emotes" toggle button
  (75x20) at x = width - 2 - 75 + `buttonShiftX`, y = height - 40 + `buttonShiftY`. When toggled
  on, the emote buttons (24x24, step 25) stack above it, 3 per row, right-aligned, grouped by tier
  (tier 0 rows first, a new row for each tier; partial rows are centred like Quark). Each button:
  a dark translucent square (darker on hover), the 16x16 icon at +4,+4, a tier badge for patreon
  emotes, and the emote's name in a bubble above it on hover. A click sends the request; the chat
  stays open. The list comes from the server (`emotes:list`: the player's emotes in config order,
  patreon ones by tier) plus the client's custom emotes. Quark expands downwards when subtitles are
  shown; Bedrock has no subtitles option, so the grid always grows upwards.
- **Keybinds** (mod client, Controls settings, unbound by default): `key.lepton.emote.<id>` for
  the 12 default emotes ("Wave"...), `key.lepton.patreon_emote.<id>` for the 6 patreon ones
  ("Dance (Patreon)"...). Quark: `quark.emote.<id>` / `quark.keybind.patreon_emote.<id>` in the
  "Quark Emotes" group (LeviLamina keybinds have no groups).
- **HUD** (mod client): see §3.
- **Vanilla-client fallback**: `/quark emote` opens a form titled "Emotes" with one button per
  emote the player may use (icon from the resource pack, "(Patreon)" after patreon emotes); a
  button plays it. `/quark emote <name>` plays directly. Custom emotes are not listed (only
  clients with the file can see them). The animation itself is visible to vanilla clients.

## 5. Config

Section `emotes` (`config/emotes.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableEmotes` | bool | true | Emotes module |
| `enabledEmotes` | string[] | `["no","yes","wave","salute","cheer","clap","think","point","shrug","headbang","weep","facepalm"]` | The enabled default emotes, in menu order |
| `customEmotes` | string[] | `[]` | Custom emote file names (client: `config/quark_emotes/<name>.emote`) |
| `customEmoteDebug` | bool | false | Re-read custom emote files every time they're triggered |
| `buttonShiftX` | int | 0 | Shift of the chat-screen buttons (client) |
| `buttonShiftY` | int | 0 | |
| `emoteTiers` | object | `{}` | Lepton: player (lower-case name or XUID) -> patron tier (Quark: its Patreon list) |
| `defaultEmoteTier` | int | 0 | Lepton: tier of everyone else (2 gives every patreon emote to all) |
| `enablePatTheDogs` | bool | true | PatTheDogs module |
| `dogsWantLove` | int | -1 | Ticks until a dog wants affection after being pet/tamed; -1 disables |
| `petAllMobs` | bool | false | Whether you can pet all mobs |
| `pettableDenylist` | string[] | `["minecraft:ender_dragon","minecraft:wither","minecraft:armor_stand"]` | Never pettable with `petAllMobs` |
| `pettableAllowlist` | string[] | `[]` | Pettable even without `petAllMobs` |

The client reads `customEmotes`, `customEmoteDebug`, `buttonShift*` from its own config; the server
decides the rest (enabled emotes and tiers are sent to mod clients).

## 6. Strings

| Key | en_US |
|---|---|
| `quark.gui.button.emotes` | Emotes |
| `quark.gui.keygroup.emote` | Quark Emotes |
| `quark.emote.<id>` | Wave, Yes, No, Clap, Cheer, Salute, Shrug, Point, Think, Facepalm, Weep, Headbang, Dab, T-Pose, Dance, Zombie, Exorcist, Jet |
| `key.lepton.emote.<id>` (RP texts) | the emote name |
| `key.lepton.patreon_emote.<id>` (RP texts) | Dance (Patreon), T-Pose (Patreon), Dab (Patreon), Jet (Patreon), Exorcist (Patreon), Zombie (Patreon) |
| `lp.emotes.form.content` | Pick an emote to play it. |
| `lp.emotes.form.patreon` | (Patreon) |
| `lp.emotes.disabled` | Emotes are disabled on this server. |
| `lp.emotes.unknown` | Unknown emote: {} |
| `lp.emotes.not_enabled` | The {} emote is disabled on this server. |
| `lp.emotes.tier` | The {} emote is a Patreon emote. |
| `lp.emotes.throttled` | Wait a moment before the next emote. |
| `lp.emotes.players_only` | Only players can use emotes. |

## 7. Bedrock differences / not ported

- **Patreon tiers** come from `emoteTiers` / `defaultEmoteTier` instead of Quark's online patron
  list (Java account names; Bedrock players have other identities).
- **Animation blending**: Bedrock adds animations; emotes are written relative to the current
  bone value (`this`), which reproduces Quark's absolute part angles. Channels of a used part that
  the emote never moves follow the live pose (Quark freezes them at the emote's first frame).
- **Body rotation** (`body_*`, custom emotes only): Bedrock's head, arms and legs are children of
  the body bone; Lepton counter-rotates them by the body's angle, which is exact for one axis at
  a time (Java parts are independent).
- **Model rotation** (`model_*`, custom emotes only) turns the `root` bone, relative to the body
  facing (Quark rotates around world axes before the body yaw).
- **Standing pose**: hidden with `blend_weight` (Quark pauses its updates); the stop on swing/hurt
  is evaluated by each client.
- **Emote sounds** are fixed at the position where they start (Quark moves them with the player),
  continuous ones are replayed every second (Quark loops the sound itself)
  and use Bedrock sound event names (`minecraft:` prefix stripped); Java sound ids that don't exist
  on Bedrock are silent.
- **Custom emote icons**: Quark loads `<name>.png` from the emote folder as a resource pack; Lepton's
  client draws icons natively from built-in pixels, so custom emotes use the `custom` icon.
  `customEmoteDebug` cannot hot-reload the baked animation (Bedrock loads resource packs at start).
- **Chat buttons** always expand upwards (no subtitles option on Bedrock).
- **PatTheDogs player sounds**: Quark plays special sounds (`pet_device`, `pet_neko`, `pet_slime`,
  `pet_wire`) for five contributors' Java UUIDs; those accounts can't exist on Bedrock, so the
  sounds (Quark assets) are not ported. Quark's player check also never matches an allowlisted
  `minecraft:player` (a Java player has no entity id); Lepton honours it (Quark issue #5538's
  intent) without sound (Bedrock has no player breath sound).
- **NuzzleGoal whine**: in Quark the goal restarts every tick near the owner, which resets the
  whine cooldown, so the whine almost never plays; Lepton plays it as the goal describes.
- Advancements of Quark tied to these modules: none.

## 8. Tests

- Unit tests (`tests/emotes/EmoteScriptTest.cpp`): script parsing and errors, tween-engine timing
  (yoyo, repeat, delays, unit, reset), easings, sounds, baking, and the committed animations.
- Selftests (server, dx 1900..1904): `emotes.builtins`, `emotes.play`, `emotes.validation`,
  `emotes.request`, `emotes.form`, `emotes.pat_wolf`, `emotes.pat_mobs`; client build also
  `emotes.client_icons`, `emotes.client_play`.
- Server e2e: `tools/e2e/server/scenarios/emotes.ps1` (selftests + log lines).
- Client e2e: `tools/e2e/client/scenarios/emotes.ps1`: third-person screenshots of the player
  emoting (wave, cheer, tpose), the HUD icon, the chat screen with the emote grid (and a hover),
  the vanilla form; run in `local`, `remote-mod` and `remote-vanilla` modes.
