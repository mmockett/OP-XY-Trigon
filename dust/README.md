# DUST

![DUST's screen: an isometric record deck on a near-black plinth, a black 7-inch with a light rim on a dark platter, its grooves and a band for the picked bar, the tonearm across it from its pillar at the right with a light counterweight on its tail, a dust brush on a post at the left](docs/screen.png)

A soul 45 on a portable record player, played as a drum kit. Every key is a
hit from the break in the middle of the record: a felt-beaten kick, a snare
cracking in a small room, hats with the ride bleeding in, congas from the
percussion overdub, handclaps, a tambourine, the bass player's note under it
all. Every key is played the way a beatmaker plays a record: drop the needle
on the bar you want, set the speed, lift the arm when you have enough.

The sound is a 1970 drum kit in a small room, through a valve compressor
and tape, cut to a 7-inch single: punchy, warm, round at the bottom, a little
dusty, tight in a boom-bap or funk groove as it comes. The same eight
controls sit on every key, the record player's: the platter's speed, the bar
the needle drops on, when the arm lifts, the stylus's tracking force, where
the needle lands, and the copy's wear, warp and the dust brush dragging on
it.

**Origin:** the break. In the 1970s DJs in the Bronx found that the best part
of a soul or funk record was the few bars where the band dropped out and the
drummer played alone, and they played those bars over and over on two
turntables. Producers then sampled them, a hit at a time, off the vinyl: the
kick from the first bar, the snare from the third, a clap from the bar where
the band comes back. A break carries everything of the record it came from:
the room it was played in, the compressor and tape it went through, the cut's
bandwidth, the dust and wear of the copy, the speed it was played at. Played
at 33 instead of 45, a break is heavier and slower; at 78, thin and fast.

## Playing it

Load DUST on a drum track. The 24 keys start as a kit laid out as stock's
drum kits are: kick, snare, three toms, clap, rim, cowbell, closed and open
hats, ride, crash, then a tambourine, two congas, bongos, a shaker, a kick
played at 33, a ghost snare, a pedal hat, a rimshot, two bass notes an
octave apart and the ride's bell.

- **Eight controls a key**, the record player's: SPEED, PICK, LIFT, WEIGHT
  on the encoders, and on the second row (press any encoder) CUE, WEAR,
  WARP, DRAG. Both rows are drawn in the header, the one the encoders turn
  bright.
- **Choose a key's model:** select the key, hold **Shift** and turn
  **encoder 1** (MACHINE). Any key can hold any of the fifteen.
- **Its mixer channel, on Shift's first row:** **encoder 2** puts the key in
  choke group 1, 2 or 3, or none (the hats start in group 1); **encoder 3**
  pans it; **encoder 4** sets its level (0 dB down to -30 dB).
- **Its pad controls and sends, on Shift's second row** (hold Shift and press
  any encoder): **encoder 1** is TUNE, -16 to +15 semitones, the platter's
  speed in finer steps (a hit tuned up is shorter, as a record played faster
  is); **encoder 2** is TONE, the player's tone control, -8 dB to +7 dB of
  treble, its loudness held; **encoders 3 and 4** (FX I and FX II) set the
  key's own sends.
- **One voice a key.** A new hit on a key cuts its last one.
- **Saving:** each key's model, controls, choke group, pan, level, TUNE,
  TONE and sends go in projects and presets.

The OP-XY gives synthesis drum kits no velocity, so a hit's strength is its
bar: PICK. Nor does it apply the track's amp envelope to a kit: a hit plays
to its own end, and its length is LIFT. A hit starts a block (1.5 ms) after
its note.

## The models

Each model is one drum of the break, recorded close and in the room: the
drum's own physics (a membrane's modes, a snare's wires, a cymbal's dense
partials, a string's) and the room, the compressor and the cut every key
shares. Every model has the same eight controls.

| Model | The hit |
| --- | --- |
| KICK | A felt beater on a ported 22-inch: a 58 Hz thump that falls into place from a few semitones up, body at 90 to 180 Hz that carries on a small speaker, the beater's slap. From bar 7 the bass player's note sits under it. |
| SNARE | A medium-tuned 14-inch: its batter head's modes, the snare side, the head's ring at 930 Hz, wires as shaped noise that buzz as the head moves, and the room around it. Early bars are ghost notes; from bar 7 the hats bleed into its microphone. |
| RIM | Early bars a cross-stick (the stick's butt on the hoop: a woody knock), late bars a rimshot (head and hoop at once: a crack and the snare behind it). |
| HAT | Closed hats: two cymbals' dense, ring-modulated partials and noise, their chick, the stick's tip. Soft bars are the tip, dark and tight; hard bars the shoulder on the edge. From bar 7 the kick sounds under it. |
| OPEN | Open hats: the same cymbals left to ring and rattle. |
| RIDE | The ping of the stick on the bow over a wash that swells after it; from bar 6 the stick moves onto the bell. |
| CRASH | A crash: a dense metal wash, its low body gone in a moment, its top darkening as it rings. |
| TOM | A tom with a stick: a pitch that falls back as the head relaxes, a woody attack; from bar 7 the snare's wires answer it. |
| CONGA | A conga: the palm's bass tone in the first bars, the open tone, the slap in the last bars. |
| BONGO | The bongos' small, high drum, open to slapped. |
| TAMB | A tambourine: its jingles striking and bouncing in bursts, more of them and harder in the late bars, the frame's thump under them. |
| COWBELL | A handheld cowbell: damped in the hand on a soft bar, ringing on a hard one. |
| CLAP | Handclaps: one person softly in the first bar, up to six in the last, each pair of hands its own burst, in the room; in bar 8 the tambourine overdub comes in on the same beat. |
| SHAKER | Maracas: seeds swelling as the shake begins and landing together on the far wall; in the last bars shaken back again. |
| BASS | A flatwound bass with a foam mute, played with the fingers: a round, short note. PICK chooses the bar and so the note the bass player plays in it (A, C, D, E, G, A an octave up, C, and A with the kick under it in bar 8); TUNE moves it in semitones. |

### The controls

Every model has the same eight. Figures are a hit with the other controls at
their defaults (tools/sweep.py), at 0, 25, 50, 75 and 100: its spectral
centroid, its strongest partial, the time it takes to fall 40 dB, its
loudness. Each control's own loudness is held by the engine's level trims,
so the figures show what it does, not how loud it makes the hit.

| Control | What it does | Measured at 0 / 25 / 50 / 75 / 100 |
| --- | --- | --- |
| SPEED | The platter: 33⅓ at 0, 45 at the middle, 78 at the top (the caption in rpm). Every frequency and every time of the hit moves with it, as a record does. | KICK's strongest partial 46, 53, 61, 79, 102 Hz; TOM's 92, 105, 121, 159, 207 Hz |
| PICK | The bar the needle drops on, 1 to 8: the drummer softer in the early bars and harder late, and what else is on the tape there (the bass under the kick, the hats in the snare's microphone, the bell of the ride, the tambourine with the claps). | SNARE's centroid 2.0, 2.3, 2.6, 2.9, 4.4 kHz; RIDE's 7.6, 7.9, 8.2, 2.1, 1.2 kHz as the stick moves to the bell; BASS's note 55, 73, 98, 131, 55 Hz |
| LIFT | When the arm lifts: the hit's length, from a clipped chop to the full ring. | KICK falls 40 dB in 0.08, 0.13, 0.22, 0.37, 0.62 s; OPEN in 0.11, 0.19, 0.37, 0.72, 1.43 s |
| WEIGHT | The stylus's tracking force, 0.5 to 5 g (2 g at its default tracks clean). Light, the loud highs splatter and the lows break up; heavy, the hit darkens, rounds and fills out at the bottom. | SNARE's centroid 1.7, 2.6, 2.6, 0.89, 0.34 kHz; HAT's 8.3, 9.1, 9.5, 8.1, 5.4 kHz |
| CUE | Where the needle lands, from 25 ms early (the groove's crackle and the last bar's room before the hit) to late, into the hit (up to 45 ms on a hat, 300 ms on a crash or ride, a swell out of the groove with the stylus's scratch). | KICK's centroid 75, 75, 75, 63, 60 Hz as a late landing skips the beater |
| WEAR | The copy's wear: crackle first, then hiss, rumble, a dulled top and the fuzz of worn loud grooves. | KICK's centroid 75, 93, 135, 203, 273 Hz; SNARE's 2.6, 1.5, 0.81, 0.74, 0.83 kHz |
| WARP | A warped disc: the pitch rides up to 150 cents over its crests twice a turn and a slower swell, the stylus lightening and the top dulling over each crest. | KICK's strongest partial 61, 63, 65, 66, 67 Hz; BASS's 55, 57, 59, 60, 61 Hz, wavering |
| DRAG | The dust brush lowered onto the record: bristle noise, and the platter dragged down, up to an octave, sagging in over a quarter second. | KICK's strongest partial 61, 57, 55, 52, 52 Hz; BASS's 55, 52, 49, 47, 46 Hz |

Over all 120 sweeps (fifteen models, eight controls, five settings) a hit's loudness stays within 1 LU on 111 and within 2 LU on 116; the most it moves is 3.7 LU (BONGO's slap at the last bar).

Defaults are clean: WEAR, WARP and DRAG start at 0, WEIGHT at 2 g (it tracks
cleanly), CUE on the beat. The kit's own warmth, its room, compressor, tape
and cut, is always there.

## Presets

The init preset is the default kit. Eight kit presets lay out the 24 keys
for a style, with each key's pan, level, TUNE, TONE and sends where the
style wants them.

| Preset | For | What it does |
| --- | --- | --- |
| **crate** | boom bap | The classic: the break at 33, heavy and a little worn, the hard bars, the bass from the first bar and the fourth. |
| **45** | funk | The break at 45: the hardest bars, cut short, hats bright, ghost notes, a tambourine and a cowbell. |
| **basement** | lo-fi hip hop | A played-out copy on a warped disc at 38 rpm, the arm heavy, the brush down on the kick and the bass. |
| **bossa** | bossa nova, Latin jazz | Soft bars: the cross-stick, bongos, a shaker, a ghosted snare, a light ride. |
| **boogaloo** | Latin soul | Congas high and low, bongos, cowbell, tambourine and a clap with the band in. |
| **dubplate** | dub, roots | A heavy arm, a deep kick at 36 rpm slowed by the brush, rimshots and the bass, sent to the effects. |
| **library** | 1960s library music, soundtracks | A clean copy, every hit let ring, the toms spread across the stereo. |
| **78** | jungle, drum and bass | The break at 78: thin and fast, a light arm crunching the loud bars, the bass played an octave down. |

Demos: [docs/demo-hits.mp3](docs/demo-hits.mp3) (the default kit, key by
key), [docs/demo-crate.mp3](docs/demo-crate.mp3) (boom bap, 90 BPM),
[docs/demo-45.mp3](docs/demo-45.mp3) (funk, 100 BPM) and
[docs/demo-basement.mp3](docs/demo-basement.mp3) (lo-fi, 78 BPM). Rendered
from the engine on a computer with each key's own pan and level, the softer
hits set by a held step's LEVEL lock as a player would on the OP-XY, with no
other processing, each normalised to -16 LUFS integrated (BS.1770) or to a
-1 dBFS peak where that is lower.

## How it works

**The recording.** A hit is a drum's physics in fixed point. A strike (a
half-sine contact, felt long and soft or stick short and hard, and a click
of noise) excites up to eight modes, each a state-variable resonator at a
frequency and decay from the drum's own ratios: a circular membrane's 1,
1.59, 2.14, 2.65 for heads, a stiff string's n f0 sqrt(1 + B n^2) weighted by
where it is plucked for the bass, inharmonic sets for a cowbell and a ride's
bell. Heads glide down into pitch as their tension settles. Wires, hands,
jingles and seeds are noise through a resonant filter in bursts; cymbals are
six square partials, three of them ring-modulated in pairs, with noise,
through a filter that gives each its tone. A small room (four lines, a few
milliseconds long, feeding back through a Householder mix) sits behind the
close microphone. Then the chain every key shares: a valve compressor that
lets the first 10 ms through, a soft valve curve, and the cut's bandwidth,
from 26 Hz to 12.5 kHz at the record's speed.

**The playback.** SPEED and TUNE scale every frequency and every time of the
recording as it is synthesised, so a hit at 33 is lower and longer with
nothing resampled. WARP and DRAG move that same rate as the hit plays: the
warp's crests a turn and a slower swell (with the stylus lightening and the
top dulling over a crest), the brush's drag a sag towards its full depth in
a quarter second. WEIGHT and WEAR act on the record's sound: a light stylus
clips and splatters the loud highs and breaks up loud lows; a heavy one
darkens, fills out the low end and rounds the peaks; a worn copy is dull,
hissing, crackling and rumbling, and its loud passages fuzz. CUE's early
landing plays the groove before the hit (its crackle and the last bar's tail
in the room); a late one brings the hit up out of the groove with the
stylus's settling scratch. LIFT fades it all in 10 ms and the stylus leaves
the groove with a soft thump.

**Level.** Every model is set by loudness, not by its peak (tools/calibrate.py
writes levels.h). Measured as a hit's loudest 400 ms, K-weighted (BS.1770),
against the SDK's half-scale ceiling, every key of the default kit sits at
its role's loudness: kicks and toms at -16.5 LUFS, everything else at -19,
each within 1 LU, its true peak (4x oversampled) at -1.0 dB or under. Each
control carries two trims, one that holds the hit's loudness as it turns and
one that tracks how far that lifts its peak; a hit comes down only by what
its controls together lift its peak past the room its default leaves. TONE
holds the level by the share of each model's loudness above 2 kHz. A
transient limiter shapes the strike's first milliseconds, and a soft knee
over the last quarter under the ceiling keeps every sample under it.

| Key | Model | Loudness (LUFS-M) | True peak (dB re ceiling) |
| --- | --- | ---: | ---: |
| 53 BD | KICK | -16.5 | -1.9 |
| 54 SD | SNARE | -19.1 | -1.8 |
| 55 HT | TOM | -16.7 | -2.5 |
| 56 MT | TOM | -16.5 | -4.0 |
| 57 LT | TOM | -16.5 | -3.2 |
| 58 CP | CLAP | -19.0 | -1.1 |
| 59 RS | RIM | -19.0 | -1.5 |
| 60 CB | COWBELL | -18.9 | -4.7 |
| 61 CH | HAT | -19.0 | -1.5 |
| 62 OH | OPEN | -19.0 | -7.5 |
| 63 RC | RIDE | -18.9 | -8.5 |
| 64 CC | CRASH | -19.1 | -9.4 |
| 65 | TAMB | -19.1 | -1.0 |
| 66 | CONGA | -19.0 | -6.3 |
| 67 | BONGO | -19.0 | -4.0 |
| 68 | SHAKER | -20.0 | -1.0 |
| 69 | KICK | -16.9 | -2.0 |
| 70 | SNARE | -18.9 | -1.5 |
| 71 | HAT | -19.2 | -1.2 |
| 72 | CONGA | -18.2 | -7.2 |
| 73 | RIM | -18.5 | -1.6 |
| 74 | BASS | -19.1 | -5.9 |
| 75 | BASS | -18.7 | -8.0 |
| 76 | RIDE | -18.6 | -9.1 |

## Screen

A record deck in isometric, drawn the way the OP-XY draws its own screens:
a near-black plinth with hairline seams, a dark platter, and on it the
record, the tonearm on its pillar and a dust brush on its post. Each encoder
owns one part of the deck in its own grey, on both pages, and the parts mean
the same on every key: the record is encoder 1, drawn hollow with a light
rim; its grooves are encoder 2, in dark grey; the arm is encoder 3, in mid
grey; the counterweight and the dust brush are encoder 4, in light grey.

Play a key and the needle drops, the record spins at its speed with the cue
sticker riding round, and it glides to rest as the hit ends, back at its
cue; the arm lifts when the hit is over. A faster speed spins faster. What
each control moves:

| Control | On screen |
| --- | --- |
| SPEED | The record: a 12-inch at 33, a 7-inch at 45, a 10-inch at 78, and how fast it spins. |
| PICK | The picked bar, a solid band in the grooves, from the rim at bar 1 to the label at bar 8; the arm swings to follow it. |
| LIFT | How high the head waits over the record: the sooner it lifts, the higher. |
| WEIGHT | The counterweight slides along the arm's tail, nearer the pillar for a heavier arm. |
| CUE | A sticker on the record's rim, ahead of the stylus for an early landing, behind it for a late one. |
| WEAR | The grooves wander, more crooked the more worn, clean at 0. |
| WARP | The head rides up and down with the warped disc as it spins. |
| DRAG | The dust brush comes down its post onto the record. |
| TUNE (pad) | How fast the record spins. |

Only the selected key's hits move it, an idle screen changes nothing, and
reduced motion keeps it still. While notes sound it changes 4.3 nodes a frame on average while a hit moves, 6.7 at its busiest setting and 7 in its busiest frame; at rest, none.

## CPU

Estimates, not measurements: the worst audio block of one voice, counted in
the instructions of its Blackfin code as the SDK's pinned compiler builds it
(tools/cpu.py), over every model at every control's ends and middle, every
control at its ends at once, and every key of the kit and its presets. A
note's setup runs in a block of its own. Only `sdk.py profile` on an OP-XY
gives a figure for the catalog.

| Model | Worst block | Share of a block |
| --- | ---: | ---: |
| KICK | 19,573 | 3.37% |
| SNARE | 19,254 | 3.32% |
| RIM | 18,856 | 3.25% |
| HAT | 18,035 | 3.11% |
| OPEN | 18,437 | 3.18% |
| RIDE | 19,196 | 3.31% |
| CRASH | 18,815 | 3.24% |
| TOM | 18,835 | 3.25% |
| CONGA | 17,031 | 2.93% |
| BONGO | 18,856 | 3.25% |
| TAMB | 18,479 | 3.18% |
| COWBELL | 16,031 | 2.76% |
| CLAP | 19,182 | 3.31% |
| SHAKER | 15,525 | 2.68% |
| BASS | 17,811 | 3.07% |

Instructions a 64-sample block, of the OP-XY's 580,352-cycle block. The worst, KICK's first blocks with eight modes struck, its room, compressor, a light arm and a worn copy all at once, is 3.4%. A block's work is what that block runs: a mode that has rung out, a layer that has finished, a stage whose control is at its default cost nothing. Up to 16 voices; a silent voice frees itself. No divides run per sample, and nothing is floating point.

## Memory

100.7 KiB in the module region (`sdk.py test --firmware`): the code, its tables and the artwork, the screen's state, and 16 voices and a spare at 1,600 bytes each.

## Building

DUST needs SDK 1.10 (synthesis drum kits with a mixer channel a key).
tools/presets.py writes the kit presets into a build.

```sh
python ../open-xy/sdk.py test --machine dust
python ../open-xy/sdk.py test --machine dust --firmware
python ../open-xy/sdk.py build --machine dust ... --out <build>
python3 dust/tools/presets.py --out <build>/candidate
sh dust/tools/ub.sh                   # the sanitizers, and g++ against clang bit for bit
```

## Files

| File | What it is |
| --- | --- |
| `kit.h` | The fifteen models, their controls' defaults, the 24 keys and the hats' choke group |
| `record.h` | The record player's controls as physical values, shared by the engine and the screen |
| `models.h` | The hits |
| `voice.h` | A voice's parts: the strike, the modes, the noise layers, the metal, the room |
| `engine.cpp` | The kit's callbacks, the chain, the playback, the level |
| `dsp.h`, `lit.h`, `tables.h`, `tables.cpp` | Fixed-point arithmetic and its tables (generated by `tools/make_tables.py`) |
| `levels.h` | Every model's gain and level trims (generated by `tools/calibrate.py`) |
| `display.cpp`, `display.h`, `art.h`, `panel.svg.fragment` | The screen (the artwork generated by `tools/make_panel.py`) |
| `tests/` | Sound, pad and screen checks, run by `sdk.py test` |
| `tools/` | Calibration, presets, demos, contact sheets, CPU counts, the undefined-behaviour check |

## Licence

MIT ([LICENSE](../LICENSE)).
