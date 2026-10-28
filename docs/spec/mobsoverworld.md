# U20 mobsoverworld: Crabs, Shibas and Oretoises

Behaviour reference: Quark 4.1-488 (`release-4.1-488+1.21.1`), modules `mobs/CrabsModule`,
`mobs/ShibaModule`, `mobs/ToretoiseModule`, their entities (`Crab`, `Shiba`, `Toretoise`), goals
(`RaveGoal`, `BarkAtDarknessGoal`, `FetchArrowGoal`, `DeliverFetchedItemGoal`), renderers/layers,
data (loot tables, recipes, tags) and `Quark.java` (brewing). Nothing is copied: models, textures and
animations are original; sounds are mapped to vanilla Bedrock sounds (§7).

## 1. Modules covered

### Crabs (`enableCrabs`)
A small beach animal. Crabs spawn on sand / red sand in beach biomes, wander, panic when hurt,
follow players holding wheat, raw chicken or fish, and breed with them. They come in three colours
(red, blue, green) with an optional mould overlay. Crabs prick other living things that bump into
them (1 cactus damage, not in Peaceful), never drown, are immune to cactus, berry bushes, lightning
and burning. A crab near a playing jukebox stops and dances ("raves"), kicking up sand. A crab struck
by lightning grows (up to scale 17): bigger crabs are fire-proof, slower, tougher and can be ridden.
They drop crab legs (food, cookable) and, when killed by a player, crab shells: brewed into the
Potion of Stability, which gives **Resilience** (+0.5 knockback resistance per level). Crabs can be
caught in a water bucket ("Bucket of Crab"). Expert fishermen buy crab legs.

### Shiba (`enableShiba`)
A tameable dog of the mountains. Tamed with bones (1 in 3), it sits on command, follows its owner,
breeds with meat, heals from meat, wears a dyeable collar and wolf armor. A Shiba fetches arrows its
owner shot (and eats Infinity arrows), carries a sword given to it in its mouth and brings items back
to its owner. When its owner holds a torch, soul torch or lantern, it searches for dark spots
(block light 0) near it, walks there, barks and shows angry particles until the spot is lit.

### Toretoise (`enableToretoise`, English name "Oretoise")
A slow, 60-health underground creature (y <= 0, dark, on stone) carrying one of five ores (coal,
iron, redstone, lapis, copper). Hitting it with a pickaxe mines the ore (deepslate-ore drops, with
the pickaxe's enchantments) instead of hurting it. Any other hit makes it stomp after 20 ticks
(damage 4 + difficulty to everything within 3 blocks). Feeding it glow berries tames it (no
despawn, 20 heart particles); fed again after the cooldown, it regrows a random ore with a 1 in
`regrowChance` chance. A piston pushing an iron rod (`lepton:breaks_toretoise_ore`) into it mines it
with an iron pickaxe.

## 2. Blocks and items

| Id | English name | Notes |
|---|---|---|
| `lepton:crab_leg` | Raw Crab Leg | food 1, saturation modifier 0.3; Food & Drinks tab after pufferfish |
| `lepton:cooked_crab_leg` | Cooked Crab Leg | food 8, saturation modifier 0.8 |
| `lepton:crab_shell` | Crab Shell | Ingredients tab after rabbit foot; only when `enableBrewing` |
| `lepton:crab_bucket` | Bucket of Crab | places water + the crab (keeps colour, mould, health, name) |
| `lepton:resilience_potion`, `lepton:long_resilience_potion`, `lepton:strong_resilience_potion` | Potion of Stability | 3:00 / 8:00 / Resilience II 1:30 |
| `lepton:resilience_splash_potion` (+ long / strong) | Splash Potion of Stability | thrown; affects living things within 4 blocks (scaled by distance like vanilla: duration * (1 - d/4)) |
| `lepton:resilience_lingering_potion` (+ long / strong) | Lingering Potion of Stability | thrown; leaves a cloud (radius 3, 30 s, 1/4 duration per application) |
| `lepton:crab_spawn_egg` (`spawn_egg` of `lepton:crab`) | Crab Spawn Egg | colours `#893c22` / `#916548` |
| `lepton:shiba_spawn_egg` | Shiba Spawn Egg | `#a86741` / `#e8d5b6` |
| `lepton:toretoise_spawn_egg` | Oretoise Spawn Egg | `#55413b` / `#383237` |

Entities: `lepton:crab` (Crab), `lepton:shiba` (Shiba), `lepton:toretoise` (Oretoise).

Recipes: `lepton:crab_leg` -> `lepton:cooked_crab_leg` in furnace, smoker, campfire and soul
campfire (0.35 xp in Quark).

Brewing (`enableBrewing`, from `Quark.java`): water + crab shell -> mundane; awkward + crab shell ->
Stability; Stability + redstone -> long; Stability + glowstone -> strong. As in vanilla, every
recipe works for normal, splash and lingering bottles, gunpowder turns a Stability potion into the
splash one and dragon's breath turns a splash one into the lingering one.

Loot (`loot_tables/entities/crab.json`): crab shell, 25% (+3% per Looting level), killed by a
player only; crab leg 0-1 (+0-1 per Looting level), cooked when the crab burns.
Oretoise: no death drops (Quark's table is empty). Ore drops are the deepslate ore drops:
coal 1 coal, iron 1 raw iron, redstone 4-5 redstone, lapis 4-9 lapis, copper 2-5 raw copper, each
with vanilla Fortune bonuses; Silk Touch drops the deepslate ore block.

Tags (Quark's, as Lepton lists): `crab_spawnable` = sand, red sand; `crab_tempt_items` = wheat,
chicken, cod, cooked cod, salmon, cooked salmon, pufferfish, tropical fish; `shiba_holdables` =
swords (`minecraft:is_sword` item tag); `shiba_lights` = torch, soul torch, lantern, soul lantern;
`lepton:breaks_toretoise_ore` (block tag) = `lepton:iron_rod` (U10). Other units may add blocks with
that tag; Lepton also accepts the id `lepton:iron_rod` directly.

## 3. Mechanics

### Crab
- Attributes: health 20, speed 0.25, armour 3, armour toughness 2 (applied natively with Java's
  armour formula: damage * (1 - min(20, max(armor / 5, armor - damage / (2 + toughness / 4))) / 25)
  for damage that armour stops), knockback resistance 0.5, size 0.9 x 0.5.
- AI (priority): panic 1.25; rave; breed 1.0; tempt 1.2 (crab_tempt_items); follow parent 1.1;
  random stroll (avoids water) 1.0; look at player 6; random look.
- Variant on first tick: 50% red, otherwise blue or green (equal); 1/3 also mouldy. Babies roll anew.
- Spawn rule: `crabsSpawnConfig` (weight 5, group 1-3, beach biomes), on `crab_spawnable` blocks,
  brightness > 8.
- Spikes: an entity (not a crab, not its passenger) touching a crab takes 1 cactus damage unless the
  difficulty is Peaceful or the crab has `NoSpike`.
- Immunities: contact (cactus / berry bush), lightning, fire tick; fire (in fire) when scale > 1.
- Lightning: when struck and its lightning cooldown is 0 and scale <= 15: max health +0.5, speed
  x0.95, armour +0.125, scale becomes 1 + min(scale + 1, 16) (1 -> 3 -> 5 ... -> 17), cooldown 150
  ticks (fire is put out while it runs). Scale >= 2: a player that is not sneaking and not holding
  crab food can ride it; such crabs cannot be bucketed and do not push.
- Rave: a jukebox that plays within 10 blocks makes the crab rave (checked every second); it stops
  moving (panic still wins), plays a dance animation and makes block-break particles of sand under
  it every 10 ticks. It stops when the jukebox stops, is out of range or is no longer a jukebox.
- Bucket: a player with a water bucket using it on a crab (scale < 2) gets a Bucket of Crab (plays
  the fill sound); emptying the bucket places water and a persistent crab (`FromBucket`).
- Underwater: never runs out of air; step height 0.6 (Java 1.0 in water).
- Trade (`addCrabLegToFishermanTrades`): fishermen sell 1 emerald for 4 crab legs at the Expert
  tier (12 uses, 30 villager xp).
- Breeding with tempt items; the baby gets a random colour.

### Resilience
- `effect.quark.resilience` "Resilience", beneficial, colour `#5b1a04`: +0.5 knockback resistance per
  level (attribute modifier, Lepton-native effect). Durations: 3600 / 9600 / 1800 (level II).
- Drinking a Stability potion gives it (and returns a glass bottle); splash / lingering as above.
  Drinking milk or dying clears it. The effect is saved with the world (`lp:mobsoverworld:effects`).
- Mod clients see the effect in the top-right of the HUD ("Resilience II 1:23"). Vanilla clients
  get an action-bar line when it starts and ends (§4).

### Shiba
- Attributes: wild 8 health, tamed 20; attack damage 4; size 0.8 x 0.8.
- AI: float; sit when ordered; bark at darkness; fetch arrow; deliver fetched item (follow owner,
  speed 1.1, until close); follow owner 1.0 (start 10, stop 2); tempt (bone) 1.0; breed; random
  stroll; look at player 8; random look.
- Taming: a bone, 1 in 3 -> tamed, sits, hearts; otherwise smoke.
- Interact (tamed): mouth item -> given to the player (hearts, whine); meat when hurt -> heal by its
  nutrition; dye -> collar colour (default red); sword (`shiba_holdables`) -> goes into its mouth;
  wolf armor (owner, adult) -> worn; shears (owner, armoured) -> armour dropped; armadillo scute
  while sitting and armour damaged -> repairs 1/8 of max durability; otherwise the owner toggles
  sitting. Sneaking with an empty hand pets it (hearts + whine; tamed only; at most every 20 ticks).
- Armour absorbs damage (not damage that bypasses wolf armour: fall, drowning, starvation, magic,
  wither, void, ...) and loses durability instead; crack sound + scute particles at each crackiness
  step.
- Fetch: while tamed, awake, not sitting, mouth empty and not already fetching: an arrow shot by
  its owner (pick-up allowed) inside the owner's box grown by 2 blocks is picked at random as the
  target. The Shiba runs to it (jumping every 10-14 ticks), takes it into its mouth within 3 blocks,
  eats creative-only/disallowed ones within 1 block, then brings it to its owner.
- Darkness: tamed, not fetching/sitting/leashed, owner holds a `shiba_lights` item in either hand,
  cooldown 0: 20 random tries within 10 blocks for an air block above a solid block with block light
  0, in sight (and, with `ignoreAreasWithSkylight`, without sky access). It walks there (1.1),
  barks every 5 ticks with angry particles until the spot gets block light or the owner stops
  holding a light (cooldown 40 ticks).
- Texture: one of three coats by unique id; 1 in 200 the rare coat; named "doge" / "rad shiba"
  (any case) use the doge / rad coats.
- Spawn rule: `shibaSpawnConfig` (weight 40, group 1-3, mountain biomes), animal rules.

### Toretoise
- Attributes: health 60, speed 0.08, knockback resistance 1, step 1, size 2 x 1 (+0.4 high with
  ore), fire immune, no fall damage, never drowns, cannot jump, cannot be leashed, no breeding.
- AI: tempt (foods) 1.25; random stroll 1.0; look at player 6; random look.
- Natural spawns (and spawn eggs) get a random ore 1-5.
- Pickaxe hit with ore: drops the ore loot, damages the pickaxe by 1, harvest sound, no damage.
- Other hit: the first hit arms a 20-tick stomp (sound at 3 ticks left; clouds and damage
  4 + difficulty to living things in x/z +-3, y +-1 when it reaches 0 on the ground; in Peaceful
  players get a small push).
- Feeding (`foods`, default glow berries): eat sound, heals 8; first time: tamed (persistent,
  hearts); later with the cooldown over: regrow chance 1 in `regrowChance` (0 = never) -> random ore,
  cooldown `cooldownTicks`, clouds + regrow sound.
- Spawn rule: `toretoiseSpawnConfig` (monster, weight 120, group 2-4, not nether/end), y <=
  `maxYLevel`, dark, on stone-like blocks (stone, deepslate, tuff, granite, diorite, andesite,
  calcite, ...), `dimensions` (overworld), not Peaceful.
- Iron rod: every tick, a moving block (piston) of `lepton:breaks_toretoise_ore` inside its box
  while the piston extends mines the ore (iron pickaxe, no enchantments).

### Implementation
- Behaviour-pack entities (`bp/entities/{crab,shiba,toretoise}.json`) give the vanilla AI goals,
  breeding, taming, sitting, healing, riding and the spike (`minecraft:area_attack`). Entity
  properties (`lepton:*`, client-synced) hold Lepton state and drive rendering.
- Native (`src/feature/mobsoverworld/`): lightning growth, rave detection (jukebox hooks), bucket,
  shiba interactions / fetching / darkness search (native path navigation), toretoise mining /
  stomp / feeding, crab armour, resilience effect, brewing mixes (`PotionBrewing::addPotionMix`),
  fisherman trade, spawn checks (`SpawningMobEvent`), spawn-rule files rewritten from the config at
  mod load.

## 4. Screens

None in Quark. Lepton adds the Resilience HUD line for mod clients (`mobsoverworld:effect` message,
drawn by `src/client/mobsoverworld`). Vanilla clients get the action-bar text
`lp.mobsoverworld.effect_start` / `lp.mobsoverworld.effect_end` instead.

## 5. Config

Section `mobsoverworld` (`config/mobsoverworld.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | |
| `enableCrabs` | bool | true | Crabs module |
| `crabsSpawnConfig` | object | `{spawnWeight 5, minGroupSize 1, maxGroupSize 3, biomes {isBlacklist false, biomeTags ["beach"]}}` | |
| `enableBrewing` | bool | true | crab shell drops and Stability brewing |
| `resilienceRequiredForAllEffects` | bool | true | advancement option (advancements not ported); kept |
| `addCrabLegToFishermanTrades` | bool | true | |
| `enableShiba` | bool | true | Shiba module |
| `shibaSpawnConfig` | object | `{40, 1, 3, biomes {false, ["mountains"]}}` | |
| `ignoreAreasWithSkylight` | bool | false | darkness search skips spots that see the sky |
| `enableToretoise` | bool | true | Toretoise module |
| `maxYLevel` | int | 0 | highest spawn y |
| `cooldownTicks` | int | 1200 | ticks after a regrow until food can regrow again |
| `foods` | string[] | `["minecraft:glow_berries"]` | foods (tempt, tame, regrow) |
| `allowToretoiseToRegrow` | bool | true | Quark advancement flag; no gameplay effect in Quark, kept |
| `regrowChance` | int | 3 | 1 in N; 0 never |
| `dimensions` | object | `{isBlacklist false, dimensions ["minecraft:overworld"]}` | |
| `toretoiseSpawnConfig` | object | `{120, 2, 4, biomes {true, ["nether", "the_end"]}}` | |

Spawn config shape: `{ "spawnWeight": int, "minGroupSize": int, "maxGroupSize": int, "biomes":
{ "isBlacklist": bool, "biomeTags": [string], "biomes": [string] } }`. `biomeTags` are Bedrock biome
tags (Java `is_beach` -> `beach`, `is_mountain` -> `mountains`, `is_nether` -> `nether`, `is_end` ->
`the_end`; Forge `is_void` has no Bedrock tag), `biomes` are biome ids. A weight of 0 disables
spawning. The spawn rule files are written from this config when the mod loads (a config change
needs a restart, as in Quark); the precise conditions are checked natively on every spawn.
Disabling a module stops natural spawns, removes its recipes/brewing/trades, and despawns its
entities.

## 6. Strings

| Key | en_US |
|---|---|
| `effect.quark.resilience` | Resilience |
| `quark.jei.hint.crab_info` | Crabs can be found in Beaches, and bred with Wheat, Chicken, or any type of Fish. |
| `quark.jei.hint.shiba_find_low_light` | If you're holding a torch, Shibas will point out dark spots for you to light up. |
| `lp.mobsoverworld.effect_start` | You feel steadier ({0}) |
| `lp.mobsoverworld.effect_end` | Resilience has worn off |
| `lp.mobsoverworld.effect_hud` | {0} {1} |

Item / entity names in `texts.d` (`item.lepton:crab_leg=Raw Crab Leg`, `entity.lepton:crab.name=Crab`,
`entity.lepton:toretoise.name=Oretoise`, potions "Potion of Stability" ...).

## 7. Bedrock differences / not ported

- Sounds: Quark's own sound files are not copied. Crabs use vanilla turtle / armadillo-like
  sounds, Shibas the wolf sounds, Oretoises the turtle / stone sounds (mapping in `Sounds.cpp`).
- Resilience is a Lepton effect, not a vanilla mob effect: Bedrock has no API to register new mob
  effects (the effect list is a fixed enum shared with every client), so the vanilla inventory
  effect list does not show it (mod clients get a HUD line, vanilla clients an action-bar text).
  Tipped arrows of Stability are not possible (bows only fire `minecraft:arrow` with vanilla
  potion ids).
- Stability potions are custom items (Bedrock potions are a fixed list of potion ids); they brew in
  the vanilla brewing stand through the native mix table, which is sent to every client.
- Crab colour preference for walking on sand (`getWalkTargetValue`) has no Bedrock stroll option;
  crabs stroll normally but spawn only on sand.
- Crab step height in water (1.0) is not separate; 0.6 everywhere. Not pushed by water is
  approximated by `minecraft:physics` defaults (Bedrock mobs are pushed by flowing water).
- Shiba "nuzzle" / "want love" goals belong to PatTheDogs (U19, `dogsWantLove`, off by default in
  Quark); petting with an empty hand while sneaking is ported here.
- Shiba mouth items render through the geometry's held-item bone (Bedrock renders mob carried
  items there); the item's exact angle differs.
- Spawn weights in the config are applied by rewriting the spawn rule files at mod load: changing
  them needs a restart.
- Advancements (crabtured, mine turtle, we're rich, paint the town; balanced diet / two by two /
  furious cocktail modifiers) are not ported (SPEC §8).
- The iron rod rule checks the rod's facing only when the moved block has a facing state.

## 8. Tests

Selftests (dx 2000..2099): `mobsoverworld.crab_variant`, `crab_lightning`, `crab_bucket`,
`crab_rave`, `crab_armor`, `crab_loot`, `brewing`, `resilience`, `fisherman_trade`,
`shiba_tame`, `shiba_interact`, `shiba_fetch`, `shiba_darkness`, `toretoise_ore`,
`toretoise_mine`, `toretoise_feed`, `toretoise_stomp`, `spawn_rules`.
E2E: `tools/e2e/server/scenarios/mobsoverworld.ps1` (runs the selftests, checks the log lines),
`tools/e2e/client/scenarios/mobsoverworld.ps1` (spawns every colour of crab, a raving crab, a giant
crab, shibas with collars/armour/sword, oretoises with each ore, gives the items; screenshots).
