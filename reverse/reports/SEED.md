# Level seed parsing — 2026-11-18

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

This replaces BDS-RE's own FNV-1a/numeric seed policy with the original
`LevelSettings::parseSeedString`. It also corrects a documentation error: the
original does **not** hash `level-seed=1` into −1997048360.

## Call path

`DedicatedServer::run()` builds its `LevelSettings` at `0x1808cfe..0x1808d3d`:

```
call Level::createRandomSeed()            ; 0x1808cec, returns a random seed
...
call PropertiesSettings::getLevelSeed()   ; 0x1808d2f
mov  -0x458(%rbp),%esi                    ; the random seed becomes the default
mov  %rax,%rdi
call LevelSettings::parseSeedString(...)  ; 0x1808d3d
```

So an absent or unusable `level-seed` resolves to a fresh
`std::random_device` draw, not to a fixed value. Immediately above, the same
function reads `PropertiesSettings::getLevelType` and compares it with the
literal `FLAT` at `0x281c28d` (0x1808cc1..0x1808cd2).

## Translated functions

| Original | Behaviour | BDS-RE |
|---|---|---|
| `LevelSettings::parseSeedString` `0x1c69910` (0x1df bytes) | The decision tree below | `bds::parse_seed_string` (`src/world/seed.cpp`) |
| `Util::hashCode` `0x1949300` | `h = 31*h + (signed char)c`, 32-bit wrap | `bds::string_hash_code` |
| `Util::isIntegral` `0x194a340` | Erase a leading `-`, then every char must be `0`..`9` | `bds::is_integral` |
| `Util::stringTrim` `0x270fa50` | Trim the set `" \t\n\r"` | `bds::string_trim` |
| `Level::createRandomSeed` `0x1c7ff30` | `random_device("default")` + `uniform_int_distribution<unsigned>(0, UINT_MAX)` | `bds::create_random_seed` |

`parseSeedString` took its argument **by value**, which is visible in the
disassembly as a copy into `-0x38(%rbp)` before `stringTrim` (0x1c69969) — the
original never mutates the caller's string.

### Decision tree (native order)

1. `size() < 2` → return the default (0x1c69923..0x1c6992c).
2. `size() == 2 && text[0] == '-'` → return the default (0x1c69942..0x1c69958).
   Any two-character dash string behaves this way, e.g. `-9`.
3. `trimmed = stringTrim(text)`; `trimmed.empty()` → default (0x1c6997a..83).
4. `trimmed[0] == '0'` **or** `trimmed[0] == '-' && trimmed[1] == '0'` →
   `hashCode(trimmed)` (0x1c69996..0x1c699d1). This is why `007` keeps its
   leading zero as text instead of becoming 7.
5. `!isIntegral(trimmed)` → `hashCode(trimmed)` (0x1c699f7..0x1c69a2c).
6. `sscanf(trimmed, "%d", &value)` at 0x1c69a54 with the format string `"%d"`
   at `0x2870b63`; `sscanf <= 0` → default (0x1c69a5c..0x1c69a60).
7. `trimmed != "-1"` **and** `value == -1` → default
   (0x1c69a66..0x1c69a82; the literal `"-1"` is at `0x2836996`). The comparison
   is against the **trimmed** string, so `" -1 "` is accepted.
8. Otherwise return `value` as `unsigned int`.

### glibc `%d` details the original inherits

`sscanf("%d")` is not a strict int parse. It saturates the intermediate at the
64-bit `long` bounds and then truncates to `int`. Reproduced deliberately:

| Input | Result |
|---|---|
| `2147483648` | `0x80000000` (INT_MIN) |
| `-2147483649` | `0x7fffffff` (INT_MAX) |
| `99999999999999999999` | `0xffffffff` → rejected as the `-1` sentinel → default |
| `-99999999999999999999` | `0` |

`hashCode` sign-extends each byte (`movsbl` at 0x194934d), so non-ASCII input
hashes as negative code units.

## The "level-seed=1 → −1997048360" claim was wrong

`reverse/reports/TERRAIN.md` recorded that the original hashed `level-seed=1`
into RandomSeed −1997048360. Running the original twice with
`level-name=S` and `level-seed=1` gives **different** `RandomSeed` values in
`worlds/S/level.dat` on every fresh world:

| run | level.dat RandomSeed |
|---|---:|
| 1 | 1615533780 |
| 2 | −1798849937 |

−1997048360 was one such random draw, not a hash of `"1"`. The cause is rule 1
above: `"1"` is one character, so `parseSeedString` returns
`createRandomSeed()`. Every seed of length ≥ 2 is stable across runs
(`12`→12, `abc`→96354, `Bedrock level`→1201671722, `123`→123), which the same
experiment confirms.

BDS-RE now reproduces this: `level-seed=1` gives a different world seed each
fresh world, and `12`/`abc`/`Bedrock level` resolve to the original's values.
The snapshot metadata records the resolved seed, so the behaviour is testable
without a client.

## Verification

`tools/re/gdb_seed_parse.py` calls the **original** `0x1c69910` in a paused
original server, building each `std::string` in the inferior and reading the
returned `unsigned int`. It makes no call to BDS-RE and replaces no result.
`tools/re/export_seed_reference.py` validates the binary hash, address and case
shape, then copies the observations into
`tests/reference/seed_bds_1.6.0.15.json`.

The corpus is **49 cases**: 14 hashed, 26 parsed as integers and 9 that fall
back to the default. `seed_reference` requires every one, plus the individual
helper values, plus `createRandomSeed`'s range and the one-character rule.

Runtime integration verified by launching BDS-RE and reading the resolved seed
back out of the version-2 snapshot metadata:

| `level-seed` | resolved seed |
|---|---:|
| `12` | 12 |
| `abc` | 96354 |
| `Bedrock level` | 1201671722 |
| `1` | different on every fresh world (matches the original) |
| `-1` | different on every fresh world (fallback, matches the original) |

`Server::level_seed` (used for `StartGame`'s `LevelSettings::seed` and the
advertisement) now reports the stored world seed, so the value the client sees
is the one the generator used.

50/50 CTest tests pass in GCC Debug.

## Still pending

- The `FLAT` comparison at 0x1808cbe selects the flat generator, but
  `FlatWorldGenerator` (0x1b6ed70) is still not compared chunk-for-chunk
  against the original. Note the original ignores `level-type=FLAT` unless this
  path is taken, which is why the level-Random oracle overrides the generator
  field in GDB.
- `level.dat` seed persistence is BDS-RE's own snapshot metadata; original
  LevelDB storage is still unimplemented.
