# Per-unit specs

[docs/SPEC.md](../SPEC.md) holds the shared parts of the spec: overview, the Quark module inventory,
conventions, configuration rules, commands and the work plan (§9). The detail of each feature unit
lives here, in `docs/spec/<unit>.md`, where `<unit>` is the unit's short name from SPEC §9
(`vslabs.md`, `stones.md`, ..., `worldbeyond.md`).

- The unit writes its file **before its first feature**, from Quark 4.1-488's behaviour
  (tag `release-4.1-488+1.21.1`), and keeps it up to date in the same PR as the code.
- Describe behaviour only. Never copy Quark code or assets.
- Workers do not edit `docs/SPEC.md`. If a shared section needs a change (a new convention, a module
  moved to another unit), say so in the PR body.

## Template

Copy these headings into `docs/spec/<unit>.md` and fill every section (write "None" where empty).

```markdown
# U<n> <unit>: <one-line summary>

## 1. Modules covered
One subsection per Quark module: what it does in Quark, in a few sentences.

## 2. Blocks and items
Table per module: id (`lepton:<quark path>`), English name, states (name, values, default),
recipes (crafting / stonecutter / smelting, with the pattern), drops (silk touch, tools),
creative tab and position, block/item tags (incl. `lepton:<feature>` tags other units may add to).

## 3. Mechanics
Precise rules with Quark's numbers (chances, ranges, durations, cooldowns), which config key
controls each, and the events/hooks the port uses.

## 4. Screens
Every screen: layout for mod clients (drawn by src/client/ui), keybinds, and the vanilla-client
fallback (form from game/Forms, or the vanilla container screen from game/VirtualContainer), or why
there is none.

## 5. Config
Section `<unit>` (`config/<unit>.json`): every key, type, Quark default, meaning. Starts from the
generated table in SPEC §5.1; document the JSON shape of structured options.

## 6. Strings
Key, en_US text (Quark's wording). The zh_CN translation lives in the lang files only.
Block/item/entity names are listed in §2 and come from RP texts.d.

## 7. Bedrock differences / not ported
Every difference from Quark and every left-out part, with the reason.

## 8. Tests
Selftest names (`<unit>.<case>`) and the dx range used (U<n>: 100n..100n+99), e2e scenarios
(`tools/e2e/{server,client}/scenarios/<unit>.ps1`) and what their screenshots show.
```
