# U22 throwables: pickarangs, torch arrows, parrot eggs, skull pikes, bottled clouds, slime buckets

Quark 4.1-488 modules of the `tools` category: Pickarang, TorchArrow, ParrotEggs, SkullPikes,
BottledCloud, SlimeInABucket (SPEC §2.1 / §9). Behaviour reference: tag `release-4.1-488+1.21.1`.
Quark 4.1-488 has two pickarang types (pickarang, flamerang); there is no "echorang" in this release.

## 1. Modules covered

### Pickarang
A pickaxe boomerang. Using it throws it (the item leaves the hand); the projectile flies straight
without gravity, mines every block it hits that its harvest level / max hardness allow (as if the
thrower mined it with the pickarang: enchantments apply), damages entities it hits as a melee hit
of the thrower, then flies back to the thrower collecting the dropped items and experience orbs
on its way and puts itself back into the slot it was thrown from. The Flamerang is the netherite
upgrade: harder blocks, more damage, flame particles, and the items it carries are fire-proof.

### TorchArrow
An arrow crafted from a torch and an arrow. When it hits a block it places a torch (standing on
top faces, wall torch on side faces) in the air block in front of the hit face; on entities it
sets them on fire like a burning arrow. Shot by bows, crossbows and dispensers.

### ParrotEggs
Feeding Beetroot Seeds to a **tamed** parrot gives a 5 % chance that it starts carrying an egg;
after `eggTime` ticks (10 minutes) it lays a parrot egg (50 %: its own colour, 50 %: a random
colour). Throwing a parrot egg hatches a baby parrot of the egg's colour. One grey parrot in 20 is
the "Special Awesome Parrot" (kotobirb texture).

### SkullPikes
A skeleton skull, zombie head or creeper head placed on top of a fence becomes a skull pike.
Monsters that see a pike within `pikeRange` blocks run away from it (bosses, raiders and the
warden are not fooled).

### BottledCloud
Using a glass bottle while standing between y=191 and y=196 bottles a cloud. Using the bottled cloud
places a temporary cloud block where you look (in mid-air too); it shrinks and disappears after
10 seconds. Using a block item on a cloud replaces the cloud with that block and gives the cloud
back in a bottle; using a glass bottle on it takes the cloud back.

### SlimeInABucket
Using a bucket on a tiny slime picks it up as a Bucket of Slime. The slime gets excited
(jumps in the bucket) while the holder is inside a slime chunk. Using the bucket on a block releases
the slime.

## 2. Blocks and items

| Id | English name (zh_CN) | Stack | Notes |
|---|---|---|---|
| `lepton:pickarang` | Pickarang (皮卡镖) | 1 | durability 800, enchantable as a pickaxe (value 10), repair: diamond; creative: Equipment, after the diamond hoe |
| `lepton:flamerang` | Flamarang (界炎镖) | 1 | durability 1040, enchantable (value 15), repair: netherite ingot; fire resistant item |
| `lepton:torch_arrow` | Torch Arrow (火把箭) | 64 | ammunition of bows/crossbows, dispensable |
| `lepton:egg_parrot_red_blue` | Red Parrot Egg (红色鹦鹉蛋) | 16 | throwable; creative: after the egg |
| `lepton:egg_parrot_blue` | Blue Parrot Egg (蓝色鹦鹉蛋) | 16 | |
| `lepton:egg_parrot_green` | Green Parrot Egg (绿色鹦鹉蛋) | 16 | |
| `lepton:egg_parrot_yellow_blue` | Cyan Parrot Egg (青色鹦鹉蛋) | 16 | |
| `lepton:egg_parrot_gray` | Gray Parrot Egg (灰色鹦鹉蛋) | 16 | |
| `lepton:bottled_cloud` | Cloud in a Bottle (瓶中云) | 64 | creative: after the milk bucket |
| `lepton:cloud` (block) | Cloud (云) | – | no collision, instant break, wool sound, drops nothing, immovable by pistons, pick block gives `lepton:bottled_cloud`; state `lepton:size` 0..9 |
| `lepton:slime_in_a_bucket` | Bucket of Slime (史莱姆桶) | 1 | named slimes: "Bucket of %s"; creative: after the tadpole bucket |
| `lepton:slime_in_a_bucket_excited` | Bucket of Slime | 1 | the excited form (swapped in natively, hidden from creative) |

Entities (behaviour pack): `lepton:pickarang`, `lepton:flamerang` (0.4×0.4, no gravity, no
collision, invulnerable), `lepton:torch_arrow` (runtime `minecraft:arrow`, own texture),
`lepton:parrot_egg_<variant>` (one per colour, thrown item projectile). Skull pikes need no entity
(see §7). The vanilla `minecraft:parrot` client entity is overridden to add the kotobirb texture.

Recipes:

| Recipe id | Type | Pattern | Result | Enabled when |
|---|---|---|---|---|
| `lepton:pickarang_heart` | shaped | `DWH` / `  W` / `  D` (D diamond, W any planks, H `lepton:diamond_heart`) | pickarang | Pickarang on, Stonelings (U21) on and the heart item exists, `neverUseHeartOfDiamond` false |
| `lepton:pickarang_no_heart` | shaped | `DWD` / `  W` / `  D` | pickarang | Pickarang on and the heart recipe is off |
| `lepton:flamerang_smithing` | smithing transform | netherite upgrade template + pickarang + netherite ingot | flamerang (keeps enchantments/damage) | Pickarang and `enableFlamerang` on |
| `lepton:torch_arrow` | shapeless | torch + arrow | 1 torch arrow | TorchArrow on |

Tags: `quark:pickarang_immune` blocks (sculk sensor, sculk, sculk vein, sculk catalyst, sculk
shrieker, reinforced deepslate; plus any block tagged `lepton:pickarang_immune`) are never mined by
a pickarang. `quark:pike_trophies`: skeleton skull, zombie head, creeper head (block tag
`lepton:pike_trophy` lets other units add more). Fences: every block whose id ends in `_fence`
(vanilla wooden fences, nether brick fence) and every block tagged `lepton:fence`.
Parrot feed (`quark:parrot_feed`): beetroot seeds.

## 3. Mechanics

### Pickarang (`enablePickarang`, `pickarangType`, `flamerangType`, `enableFlamerang`)
- **Throw** (use the item, main hand): the stack leaves the hand; a projectile spawns at the eyes
  and flies along the look direction with speed `1.5 + 0.325 × efficiency` blocks/tick plus the
  thrower's motion (vertical motion only when airborne). Sound `entity.pickarang.throw` (volume
  `0.5 + 0.14 × efficiency`). Cooldown `cooldown − efficiency` ticks (not in creative).
- **Flight**: no gravity; drag 0.99 per tick (0.8 in water). Each tick the segment to the next
  position is checked: entities first (box inflated by 1, alive, not spectators, not the
  thrower, not hit before), then blocks (collision shapes, no fluids).
- **Block hit**: counts as a hit (unless piercing and the block isn't solid). The block is mined
  when the pickarang can "harvest" it: hardness ≥ 0, equivalent hardness ≤ `maxHardness`, not in
  `pickarang_immune` (creative throwers mine anything breakable). Equivalent hardness =
  hardness × (1 when the pickarang counts as the right tool, else 10/3). The right tool: a pickaxe
  (or axe when `canActAsAxe`, shovel when `canActAsShovel`, hoe when `canActAsHoe`) of tier
  `harvestLevel` (0 wood, 1 stone, 2 iron, 3 diamond, 4+ netherite) that can harvest the block.
  Mining uses the thrower's game mode with that tool carrying the pickarang's enchantments
  (fortune, silk touch), so drops follow the vanilla loot tables; the pickarang loses 1
  durability (Unbreaking applies). Otherwise it clanks (`entity.pickarang.clank`, block
  particles) and returns.
- **Entity hit**: the thrower attacks the entity holding the pickarang (damage = attack damage
  of the item: 1 + `attackDamage`; enchantments such as Fire Aspect apply); the pickarang loses 2
  durability. Hitting another pickarang makes both return. Hitting an entity without hurting it
  clanks.
- **Piercing** (from an anvil book): the pickarang keeps flying until it has hit
  `piercing + 1` things; after each hit it slows to 80 %.
- **Return**: after `timeout` ticks without a hit, after its hits, or when leaving the world
  border (`entity.pickarang.spark`). Returning, it flies through blocks towards the thrower's
  body (`0.7 + 0.325 × efficiency` blocks/tick) and picks up item entities and experience orbs
  within 2 blocks, carrying them. Within `sqrt(3.25 + 0.25 × efficiency)` blocks of the thrower it
  is caught (`entity.pickarang.pickup`): it goes back to its slot if empty, otherwise into the
  inventory or dropped; carried items and experience go to the thrower.
- If the thrower is gone (offline, dead, other dimension) the pickarang drops as an item.
- **Flamerang**: flame particles (40 % per tick); carried items and the flamerang item entity are
  immune to fire and lava.
- **Held**: attack damage 3 (pickarang) / 4 (flamerang), attack speed −2.8; melee hits cost 2
  durability. `durability` −1 = unbreakable.

### TorchArrow (`enableTorchArrow`, `extinguishOnMiss`)
- Bows and crossbows use the first ammunition found (off hand, then inventory order): a torch
  arrow before a later arrow is shot as a torch arrow. Dispensers shoot it too.
- Block hit (server): position = hit block + hit face. If that block is air or replaceable and
  the face isn't the bottom face, a torch (top face) or wall torch facing away from the block (side
  faces) is placed if it can survive there; the place sound plays and the arrow disappears.
  Otherwise it sticks like a normal arrow.
- Entity hit: the target burns for 5 seconds (like a burning arrow); the arrow itself shows no
  fire.
- Picking a stuck torch arrow up gives a torch arrow, or a plain arrow when `extinguishOnMiss`.
- Flame trail particles while flying.

### ParrotEggs (`enableParrotEggs`, `chance`, `eggTime`, `enableKotobirb`)
- Interacting with a parrot holding beetroot seeds (main hand, else off hand): if the parrot has no
  egg timer and is tamed, the interaction is consumed: one seed is used (not in creative), sound
  `mob.parrot.eat` and with probability `chance` the egg timer starts at `eggTime` ticks (happy
  villager particles), otherwise smoke particles. With a running timer: heart particles only.
  Untamed parrots keep the vanilla behaviour (taming).
- Each tick of a loaded parrot with a timer decrements it; at 1 the parrot plops
  (`mob.chicken.plop`) and drops an egg: 50 % its own variant, else a random one of the five.
  Timers are stored per parrot in the world DB (`lp:throwables:parrots`).
- Throwing an egg (sound `random.bow`, power 1.5, inaccuracy 1.0, not consumed in creative): on any
  hit it breaks (item particles) and spawns a parrot of the egg's colour as a **baby** (age −24000:
  grows up after 20 minutes). Hit entities take 0 damage (knockback only).
- Kotobirb: a grey parrot (variant 4) whose unique id is divisible by 20 renders with the kotobirb
  look when `enableKotobirb`.

### SkullPikes (`enableSkullPikes`, `pikeRange`)
- Placing a pike trophy whose block below is a fence creates a pike at the block centre (stored in
  the world DB, `lp:throwables:pikes`). A pike disappears when the trophy or the fence is gone.
- 40 % per tick a pike emits ash (5 %: warped spore) at its centre −0.25.
- Every 10 ticks, monsters within a box of `pikeRange` horizontally and 3 vertically that see a pike
  (clear line between eye heights) within `pikeRange` and that are not attacking anything run to a
  random position up to 16 blocks away (7 vertically) leading away from the closest pike, at speed
  1.0, 1.2 when within 7 blocks of the pike. The position must be farther from the pike than the mob
  is. Fooled: zombies (all kinds), skeletons (all kinds), creepers, spiders, endermen, endermites,
  silverfish, blazes, guardians, vexes, piglins, zoglins, breezes, creakings and Lepton monsters.
  Not fooled: raiders (pillager, vindicator, evoker, witch, ravager, illusioner), the warden, the
  wither, the ender dragon.

### BottledCloud (`enableBottledCloud`, `cloudLevelBottom`, `cloudLevelTop`)
- Using a glass bottle (not on water) with the player's feet strictly between
  `cloudLevelBottom` and `cloudLevelTop` consumes one bottle and gives a bottled cloud.
- Using a bottled cloud: ray from the eyes (reach 4.5, creative 5; fluids count). If it hits a
  block, the target is that block or the block in front of the hit face if not air; on a miss it's
  the block at the end of the reach. If the target is air and inside the world height, a cloud is
  placed, one bottled cloud is used and a glass bottle returned (not in creative), cooldown 10
  ticks.
- A cloud lives 200 ticks; its size shrinks from 0.6 of a block to nothing (state `lepton:size`,
  10 steps), with cloud particles every 20 ticks (`10 − elapsed/20` puffs). Then it is removed.
- Using a glass bottle on a cloud removes it and turns the first glass bottle of the inventory
  (from the selected slot) into a bottled cloud. Using a block item on a cloud places that block
  there instead (placement as if clicked on top), plays the place sound, uses the item and turns
  the first glass bottle of the inventory into a bottled cloud (not in creative).

### SlimeInABucket (`enableSlimeInABucket`)
- Interacting with a size-1 slime holding a bucket (main hand, else off hand) stores the slime
  (its full NBT) in a Bucket of Slime and removes it; a stack of buckets loses one and the bucket of
  slime goes to the inventory or is dropped.
- Using a Bucket of Slime on a block releases the slime at the block centre + the clicked face,
  restoring its custom name (see §7); without data a tiny slime with 1 health and
  speed 0.3 spawns. Sound `bucket.empty_water`. The bucket stays empty (`minecraft:bucket`) unless
  in creative.
- Every 10 ticks, a Bucket of Slime in a player's inventory becomes the excited form when the
  player is in a slime chunk, and back otherwise. Slime chunks are Bedrock's: chunk (x, z) is a slime
  chunk when `MT19937(x × 0x1f1f1f1f xor z)() mod 10 == 0`.
- The bucket's name is "Bucket of <name>" when the slime had a custom name.

## 4. Screens
None. Every feature works with vanilla clients through the resource pack (items, the cloud block,
entity renders, particles, sounds).

## 5. Config
Section `throwables` (`config/throwables.json`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enablePickarang` | bool | true | Pickarang module |
| `pickarangType` | object | `{timeout 20, harvestLevel 3, durability 800, maxHardness 20.0, attackDamage 2, cooldown 10, canActAsHoe false, canActAsShovel true, canActAsAxe true}` | Pickarang (Quark `pickarang`) |
| `flamerangType` | object | `{timeout 20, harvestLevel 4, durability 1040, maxHardness 20.0, attackDamage 3, cooldown 10, canActAsHoe false, canActAsShovel true, canActAsAxe true}` | Flamerang (Quark `flamerang`) |
| `enableFlamerang` | bool | true | |
| `neverUseHeartOfDiamond` | bool | false | Use the recipe without the Heart of Diamond even if it is enabled |
| `enableTorchArrow` | bool | true | TorchArrow module |
| `extinguishOnMiss` | bool | false | |
| `enableParrotEggs` | bool | true | ParrotEggs module |
| `chance` | double | 0.05 | The chance feeding a parrot will produce an egg |
| `eggTime` | int | 12000 | How long it takes to create an egg |
| `enableKotobirb` | bool | true | Enable Special Awesome Parrot |
| `enableSkullPikes` | bool | true | SkullPikes module |
| `pikeRange` | double | 5 | |
| `enableBottledCloud` | bool | true | BottledCloud module |
| `cloudLevelBottom` | int | 191 | |
| `cloudLevelTop` | int | 196 | |
| `enableSlimeInABucket` | bool | true | SlimeInABucket module |

Pickarang type fields: `timeout` (ticks before returning), `harvestLevel` (2 iron, 3 diamond,
4 netherite), `durability` (−1 unbreakable), `maxHardness` (22.5 ender chest, 50 obsidian),
`attackDamage` (when swung), `cooldown` (ticks), `canActAsHoe/Shovel/Axe`.

A disabled module's recipes are not loaded, its items are removed from players' inventories
(every second) and its interactions do nothing.

## 6. Strings
Mod lang (`assets/lang/*/throwables.json`):

| Key | en_US |
|---|---|
| `item.quark.slime_in_a_bucket.named` | Bucket of %s |
| `death.attack.quark.pickarang` | %1$s was surprised by %2$s's pickarang coming back around |
| `quark.subtitles.throw_pickarang` | Pickarang is flung |
| `quark.subtitles.clank_pickarang` | Pickarang clanks |
| `quark.subtitles.spark_pickarang` | Pickarang is reflected |
| `quark.subtitles.pickup_pickarang` | Pickarang is taken |
| `quark.subtitles.parrot_egg` | Parrot plops |

Item/block/entity names: RP `texts.d/*/throwables.lang` (§2; entities: Pickarang, Flamarang,
Torch Arrow, Parrot Egg).

## 7. Bedrock differences / not ported
- **Skull pike entity**: Quark keeps an invisible entity on each pike; Lepton keeps the pike
  positions in the world DB instead (same behaviour, nothing to render).
- **Flee AI**: Bedrock mob AI is data-driven; the flee "goal" is applied natively every 10 ticks
  through the mob's navigation, only while the mob has no attack target (Quark's goal sits at
  priority 3, below attack goals).
- **Baby parrots**: Bedrock parrots have no baby form; a hatched parrot is drawn at half size and
  grows to full size after 24000 ticks (tracked in `lp:throwables:babies`).
- **Kotobirb**: drawn as a tinted grey parrot (Lepton overrides the `minecraft:parrot` client
  entity with an extra render controller keyed on the mark variant the server sets); Quark's
  texture is not copied.
- **Excited bucket animation**: Bedrock item icons cannot animate; the excited form is a separate
  item with its own (static) icon, swapped in natively. Shiny slime buckets (VariantAnimalTextures,
  U18) are not drawn.
- **Death message**: Bedrock has no custom damage types; a pickarang kill shows the vanilla
  "killed by" message of the thrower (the key `death.attack.quark.pickarang` is shipped for
  completeness).
- **Held pickarang**: as in Quark it is no mining tool (destroy progress 0 for every block that
  takes time to break; hooked natively, so a vanilla client on BDS sees the crack animation but the
  server never breaks the block). Pickarang durability in the item tooltip comes from the pack
  (800 / 1040); a different `durability` config is enforced natively.
- **Slime data**: the slime's full NBT is stored in the bucket; on release the custom name is
  restored onto a fresh tiny slime (Bedrock has no safe "load actor from NBT" entry point; a tiny
  slime's other state, e.g. its 1 health, is the default anyway).
- **Flamerang trigger advancement** and the other advancements: not ported (SPEC §8).
- **Dispensed parrot eggs**: not dispensable, as in Quark (its dispenser behaviour is commented
  out).
- **Cloud wobble**: the cloud shrinks in 10 steps (block states) instead of a smooth sine wobble.

## 8. Tests
Selftests (dx 2200..2299): `throwables.config` (defaults), `throwables.items` (items, recipes),
`throwables.pickarang_mine` (2200: throw at stone, block mined, drops and pickarang return),
`throwables.pickarang_immune` (2210: obsidian / reinforced deepslate clank), `throwables.pickarang_entity`
(2220: damage to a mob, 2 durability), `throwables.pickarang_held` (2280: a held pickarang does not
mine, a melee hit costs 2 durability), `throwables.torch_arrow` (2230: arrow onto a wall/floor
places torches), `throwables.parrot_egg` (2240: feeding, laying, hatching), `throwables.skull_pike`
(2250: pike creation, removal, a husk flees), `throwables.cloud` (2260: bottling, placing,
replacing, expiry), `throwables.slime_bucket` (2270: pick up / release / slime chunk),
`throwables.slime_chunk` (pure algorithm check).
E2E: `tools/e2e/server/scenarios/throwables.ps1` (all selftests); `tools/e2e/client/scenarios/throwables.ps1`
(gives the items, places a cloud, spawns the entities and a skull pike, screenshots the hotbar,
the cloud, a thrown pickarang and a torch arrow).
