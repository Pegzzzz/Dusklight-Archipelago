# Twilight Princess Archipelago (Dusklight): Player Tutorial

This guide takes you from nothing installed to finishing a multiworld with *Twilight Princess* on
Dusklight. It is written for players: no programming needed. If you only want the short version,
see the [setup guide](../apworld/tp_dusklight/docs/setup_en.md).

## Contents

- [What this is and what you need](#what-this-is-and-what-you-need)
- [Installing Dusklight](#installing-dusklight)
- [Installing the mod](#installing-the-mod)
- [Installing the APWorld into Archipelago](#installing-the-apworld-into-archipelago)
- [Making your options file (YAML)](#making-your-options-file-yaml)
- [Generating and hosting the multiworld](#generating-and-hosting-the-multiworld)
- [Creating your Archipelago save in Dusklight](#creating-your-archipelago-save-in-dusklight)
- [Playing](#playing)
- [Moving a save to another device or reinstalling](#moving-a-save-to-another-device-or-reinstalling)
- [Updating the mod and the APWorld](#updating-the-mod-and-the-apworld)
- [Troubleshooting and FAQ](#troubleshooting-and-faq)
- [Credits](#credits)

---

## What this is and what you need

[Archipelago](https://archipelago.gg) is a multiworld randomizer: several players, often playing
different games, have their items shuffled together. Your Clawshot might be in someone else's game,
and their sword might be in one of your chests. When you open that chest, the item is sent to them
over the internet, and the other way around.

This project brings *Twilight Princess* into Archipelago through
[Dusklight](https://twilitrealm.dev), the PC and mobile reimplementation of the game. It is built
on the official [Dusklight Randomizer](https://github.com/TwilitRealm/dusklight-randomizer), so the
game is patched the same way as a randomizer seed (opened areas, cutscene skips, item behaviour,
logic), but the items come from the whole multiworld.

You need:

1. **Dusklight 2.0 or newer**, and **your own copy of the game**. Dusklight does not include any of
   the game's files: you provide a disc image dumped from your own GameCube or Wii disc (all retail
   releases are supported except the Korean Wii release).
2. **The two files of this project**, from the same release:
   - `tp_archipelago.dusk`: the Dusklight mod. One file for every platform.
   - `tp_dusklight.apworld`: the Archipelago world. Only the person who generates the multiworld
     really needs it, but players can use it to make their options file.
3. **[Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.4 or newer**, to make
   your options file and to generate the multiworld.

### Where to download the two files

Both files are published on the project's
[releases page](https://github.com/Pegzzzz/Dusklight-Archipelago/releases).

Until the first release is published there, you can get them from the project's build page:

1. Sign in to GitHub (downloading build files requires an account).
2. Open the [Actions tab](https://github.com/Pegzzzz/Dusklight-Archipelago/actions) and pick the
   most recent **Build** run with a green check mark.
3. At the bottom of the run's page, under **Artifacts**, download **release**.
4. Unzip it. It contains `tp_archipelago.dusk` and `tp_dusklight.apworld`.

Always take both files from the same release or the same build run. See
[Updating the mod and the APWorld](#updating-the-mod-and-the-apworld) for why.

---

## Installing Dusklight

Follow Dusklight's official [installation guide](https://twilitrealm.dev/install/). In short:

- **Windows:** download the release for your PC (x86_64, or ARM64 for ARM devices), extract the ZIP,
  and run `dusklight.exe`.
- **macOS:** download the release for your Mac (Apple silicon or Intel) and open `Dusklight.app`, or
  install it with Homebrew (`brew install dusklight`).
- **Linux:** download the AppImage for your architecture, make it executable, and run it.
- **Android:** download the ARM64 APK and install it.
- **iOS:** Dusklight has a separate sideloading guide (AltStore or iloader). Read the iOS note in
  the next section before you go further: this mod cannot currently be added to the iOS app.

The first time you start Dusklight, it asks for your disc image. Its
[FAQ](https://twilitrealm.dev/faq/) explains how to dump your disc.

Play the normal game for a minute to make sure Dusklight works before adding the mod.

---

## Installing the mod

The mod is called **Twilight Princess Archipelago** in Dusklight. It adds a new game mode named
**Archipelago**.

### Windows, macOS and Linux

Use either method.

**Drag and drop (easiest):**

1. Start Dusklight.
2. Drag `tp_archipelago.dusk` from your file manager onto the Dusklight window.
3. Dusklight asks **Install mods?** Check that the list shows *Twilight Princess Archipelago*, then
   press **Install 1**.

**Copy the file yourself:**

1. Close Dusklight.
2. Copy `tp_archipelago.dusk` into Dusklight's `mods` folder:
   - Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
     (paste that path into the File Explorer address bar)
   - macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`
     (in Finder, use **Go > Go to Folder...**)
   - Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
3. Create the `mods` folder if it does not exist yet.
4. Start Dusklight.

If you moved Dusklight's data folder somewhere else in its settings, use the `mods` folder inside
that data folder instead.

### Android

Dusklight on Android loads mods from the `mods` folder inside its data folder, the same way as on
a computer. Copy `tp_archipelago.dusk` into that `mods` folder with a file manager, then restart
Dusklight. Where the data folder is depends on your device and on Dusklight's settings; if you
cannot find it, ask in Dusklight's community (see the [Dusklight site](https://twilitrealm.dev)).

### iOS

Not available yet for players. On iOS, Dusklight runs mods that contain program code only when they
are bundled into the app as it is installed; they cannot be added afterwards, and the in-game mod
browser on iOS only shows mods without code. This mod contains code. Twilit Realm has said they are
working on a way to download an app with mods bundled in from their mod website; until then there is
no ready-made iOS build that includes this mod.

### Check that the mod is active

1. On Dusklight's start screen, press **Mods**. (In game, the same window is under **Mods** in the
   Dusklight menu.)
2. Find **Twilight Princess Archipelago** in the list. Its status should be **Active**.
3. If it says **Disabled**, select it and press **Enable**. If it says **Failed**, see
   [Troubleshooting](#the-mod-shows-failed-in-the-mods-window).

Mods are enabled when they are installed, so normally there is nothing to do.

### Other mods and the regular Randomizer

The Archipelago mod is its own game mode with its **own save files**:

- It can be installed next to the **Dusklight Randomizer** (which comes with Dusklight) and next to
  your other Dusklight mods.
- Archipelago saves are kept apart from your normal saves and from your Randomizer saves. Playing
  one never touches the others.
- You choose which game mode to play on Dusklight's start screen (see
  [Creating your Archipelago save](#creating-your-archipelago-save-in-dusklight)).

---

## Installing the APWorld into Archipelago

An APWorld teaches Archipelago a new game. Do this on the computer that will make options files or
generate the multiworld.

1. Install [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.4 or newer.
2. Open the **Archipelago Launcher**.
3. Press **Install APWorld** and select `tp_dusklight.apworld`.
   - You can also drag the file onto the Launcher, or (on Windows) double-click the file.
   - Or copy the file by hand into the `custom_worlds` folder of your Archipelago installation.
4. If the Launcher says a restart is required, close it and open it again.

The game is now known to Archipelago as **Twilight Princess Dusklight**.

Only install APWorlds and mods from sources you trust: both run code on your computer.

---

## Making your options file (YAML)

Every player in a multiworld provides one options file (a `.yaml` file). It holds your player name
and how you want your game randomized.

This game is a custom world, so it is **not** on the archipelago.gg website's options pages. Make
your file with your own Archipelago installation instead.

### Get the template

1. Install the APWorld (previous section).
2. In the Archipelago Launcher, press **Generate Template Options**.
3. A folder opens: `Players/Templates` inside your Archipelago folder. Copy
   `Twilight Princess Dusklight.yaml` somewhere else and open it in a text editor.
4. Set `name:` to your player name. This is the **slot name** you will type in Dusklight later
   (at most 16 characters, and it is case-sensitive).
5. Change the options you want. Every option in the template has a description.

Recent Archipelago versions also have an **Options Creator** in the Launcher, which lets you fill in
options with menus instead of a text editor.

### The options, by topic

Values are written exactly as they must appear in the YAML. Defaults are in bold. Options not listed
in your file keep their default.

#### Archipelago options

| Option | Values | What it does |
|---|---|---|
| `death_link` | **false**, true | DeathLink: when you die, everyone with DeathLink dies, and the other way around. Can also be switched in game. |
| `accessibility`, `progression_balancing`, `local_items`, `non_local_items`, `start_inventory`, `start_inventory_from_pool`, `exclude_locations`, `priority_locations`, `start_hints`, ... | | Archipelago's usual options. They work as in other games. |

#### Access requirements (the road to Ganondorf)

| Option | Values | What it does |
|---|---|---|
| `hyrule_barrier_requirements` | open, **vanilla**, fused_shadows, mirror_shards, dungeons, poe_souls, hearts | What dispels the barrier around Hyrule Castle. Vanilla: clearing Palace of Twilight. The others use the matching count below. |
| `hyrule_barrier_fused_shadows` | 1 to 3 (**3**) | Fused Shadows needed for the barrier. |
| `hyrule_barrier_mirror_shards` | 1 to 4 (**4**) | Mirror Shards needed for the barrier. |
| `hyrule_barrier_dungeons` | 1 to 8 (**5**) | Dungeons to clear for the barrier. |
| `hyrule_barrier_poe_souls` | 1 to 60 (**20**) | Poe Souls needed for the barrier. |
| `hyrule_barrier_hearts` | 4 to 20 (**10**) | Hearts needed for the barrier. |
| `hyrule_castle_big_key_requirements` | **none**, fused_shadows, mirror_shards, dungeons, poe_souls, hearts | What opens the Hyrule Castle big key gate. None: the gate is open and the Hyrule Castle Big Key is shuffled like other big keys. |
| `hyrule_castle_big_key_fused_shadows`, `..._mirror_shards`, `..._dungeons`, `..._poe_souls`, `..._hearts` | same ranges and defaults as the barrier counts | The counts for the big key gate. |
| `palace_of_twilight_requirements` | open, fused_shadows, mirror_shards, **vanilla** | What opens the Mirror of Twilight. Fused Shadows: all 3. Mirror Shards: all 4. Vanilla: complete City in the Sky. |
| `faron_woods_logic` | **closed**, open | Closed: Midna keeps you in Faron Woods until Forest Temple is done. |
| `mirror_chamber_access` | **open**, barrier, closed | Barrier: blocked until Stallord is defeated. Closed: only reachable with its portal or from Palace of Twilight. |
| `goron_mines_entrance` | **closed**, no_wrestling, open | Closed: climb to the Sumo Hall and wrestle the Goron Elder. Open: the elevator shortcut is open. |
| `temple_of_time_sword_requirement` | **none**, wooden_sword, ordon_sword, master_sword, light_sword | Sword you must put in the pedestal to open the door to the past. |
| `lakebed_does_not_require_water_bombs` | **false**, true | The rock at Lakebed Temple's entrance is gone. |
| `arbiters_does_not_require_bulblin_camp` | **false**, true | Bulblin Camp starts cleared. |
| `snowpeak_does_not_require_reekfish_scent` | **false**, true | You start with the Reekfish scent. |
| `sacred_grove_does_not_require_skull_kid` | **false**, true | The Lost Woods Skull Kid chase starts completed. |
| `city_does_not_require_filled_skybook` | **false**, true | The cannon to City in the Sky is at Lake Hylia from the start. |

The goal itself is always the same: **defeat Ganondorf**. These options decide what you need to
reach him.

#### Shuffled locations and items

| Option | Values | What it does |
|---|---|---|
| `golden_bugs` | **false**, true | Shuffle the 24 Golden Bug locations. |
| `sky_characters` | **false**, true | Shuffle the Sky Character locations. |
| `gifts_from_npcs` | **false**, true | Shuffle items characters give you. |
| `shop_items` | **false**, true | Shuffle shop items. |
| `hidden_skills` | **false**, true | Shuffle the Golden Wolf (Hidden Skill) locations. |
| `hidden_rupees` | **false**, true | Shuffle rupees hidden in tricky spots. |
| `freestanding_rupees` | **false**, true | Shuffle rupees lying in the open. |
| `poe_souls` | **vanilla**, overworld, dungeon, all | Which Poes give shuffled items instead of Poe Souls. |
| `ilia_memory_quest` | **vanilla**, letter, invoice, statue, charm | How far into Ilia's memory quest you start. The chosen quest item is shuffled. |
| `item_scarcity` | **vanilla**, minimal, plentiful | Minimal removes unneeded items (heart containers and pieces, Hawkeye...). Plentiful adds an extra copy of major items and keys. |
| `trap_item_frequency` | **none**, few, many, mayhem, nightmare | How many of your filler items become Foolish Items (traps that look like other items). |

More shuffled location types means more checks, a longer game, and more room for other players'
items.

#### Dungeon items

`small_keys`, `big_keys` and `maps_and_compasses` all accept the same placement choices:

| Value | Where the items can be |
|---|---|
| **vanilla** | Where they are in the original game. |
| own_dungeon | Inside their own dungeon. |
| any_dungeon | Inside any dungeon of your world. |
| overworld | Outside dungeons, in your world. |
| own_world | Anywhere in your own world. |
| anywhere | Anywhere in the multiworld, including other players' games. |
| keysy (`small_keys` and `big_keys` only) | No keys: locked doors (or boss doors, for big keys) start open. |
| start_with (`maps_and_compasses` only) | You start with every map and compass. |

Small keys include the Ordon Pumpkin and Ordon Cheese for Snowpeak Ruins. Big keys include the Goron
Mines key shards and the Snowpeak Bedroom Key.

| Option | Values | What it does |
|---|---|---|
| `dungeon_rewards_can_be_anywhere` | **false**, true | False: Fused Shadows and Mirror Shards are at the end of dungeons. True: anywhere in the multiworld. |
| `small_keys_on_bosses` | **false**, true | Allow your small keys on boss heart containers and dungeon rewards. |
| `unrequired_dungeons_are_barren` | false, **true** | Dungeons you do not need to beat the game hold nothing anyone needs (no progression items from any world). |

#### Story and quality of life

| Option | Values | What it does |
|---|---|---|
| `skip_prologue` | false, **true** | Start with the prologue done. Recommended: your first swords may be in someone else's game. |
| `faron_twilight_cleared` | **false**, true | Start with the Castle Sewers and Faron Twilight done. |
| `eldin_twilight_cleared` | **false**, true | Start with Eldin Twilight done. |
| `lanayru_twilight_cleared` | **false**, true | Start with Lanayru Twilight done. |
| `skip_midnas_desperate_hour` | **false**, true | Start with Midna's Desperate Hour done. |
| `skip_minor_cutscenes` | **false**, true | Skip area introductions and Midna explanations. |
| `skip_major_cutscenes` | **false**, true | Automatically skip every skippable cutscene. |
| `unlock_map_regions` | **false**, true | Start with the map filled in as far as possible. |
| `open_door_of_time` | **false**, true | The Temple of Time statue starts in place and the big door is open. |
| `active_goron_mines_magnets` | **false**, true | Goron Mines magnet switches start on (except the highest one in the main room). |
| `lower_hyrule_castle_chandelier` | **false**, true | One Hyrule Castle main hall chandelier starts lowered. |
| `skip_bridge_donation` | **false**, true | The Eldin-Castle Town bridge builds itself once Eldin and Lanayru Twilight are done. |
| `starting_form` | **human**, wolf | Wolf also turns `skip_prologue` on. |
| `starting_time_of_day` | morning, **noon**, evening, night | Time of day when you start. |
| `bonks_do_damage` | **false**, true | Bonking into walls hurts. |

#### Logic assumptions (match them to your Dusklight settings)

The logic decides which items you are expected to use where. These options tell it how your
Dusklight is set up, so set them to match your own Dusklight settings.

| Option | Values | Matching Dusklight setting |
|---|---|---|
| `logic_transform_anywhere` | false, **true** | **Settings > Cheats > Can Transform Anywhere.** On by default here, so turn that Dusklight setting on, or set this option to false. |
| `logic_increase_wallet_capacity` | **false**, true | **Settings > Gameplay > Bigger Wallets.** |
| `logic_damage_multiplier` | **vanilla**, double, triple, quadruple, ohko | **Settings > Gameplay > Damage Multiplier** (1×, 2×, 3×, 4×). `ohko` matches **Instant Death**. |
| `back_slice_as_sword` | **false**, true | Logic may expect back slicing without a sword to deal damage. |
| `ball_and_chain_webs` | **false**, true | Logic may expect the Ball and Chain to break webs. |

#### What is always turned off

Some Dusklight Randomizer features are not used in Archipelago:

- **In-game hints** (hint signs, Midna's hints, Agitha's hints) are off, because they would describe
  other players' items. Use Archipelago's `!hint` command instead (see
  [Playing](#the-archipelago-window)).
- **Entrance randomization** is not supported yet.
- The randomizer's own plandomizer is not used; Archipelago's `plando_items` works.

### An example options file

This file is valid as written (it was test-generated). Copy it and change `name:`.

```yaml
name: Link
description: Example options for Twilight Princess Dusklight
game: Twilight Princess Dusklight
requires:
  version: 0.6.4

Twilight Princess Dusklight:
  # Archipelago options
  progression_balancing: 50
  accessibility: full
  death_link: false

  # Access requirements: what opens the way to Ganondorf
  hyrule_barrier_requirements: fused_shadows
  hyrule_barrier_fused_shadows: 3
  hyrule_castle_big_key_requirements: none
  palace_of_twilight_requirements: vanilla
  faron_woods_logic: open
  goron_mines_entrance: no_wrestling
  temple_of_time_sword_requirement: none

  # Shuffled locations and items
  golden_bugs: true
  sky_characters: true
  gifts_from_npcs: true
  shop_items: false
  hidden_skills: false
  poe_souls: vanilla
  item_scarcity: vanilla
  trap_item_frequency: few

  # Dungeon items
  small_keys: own_dungeon
  big_keys: own_dungeon
  maps_and_compasses: start_with
  dungeon_rewards_can_be_anywhere: false
  unrequired_dungeons_are_barren: true

  # Story and quality of life
  skip_prologue: true
  faron_twilight_cleared: true
  eldin_twilight_cleared: true
  lanayru_twilight_cleared: true
  skip_minor_cutscenes: true
  skip_major_cutscenes: true

  # Logic assumptions: match these to your Dusklight settings
  logic_transform_anywhere: true
  logic_increase_wallet_capacity: false
  logic_damage_multiplier: vanilla
```

With `logic_transform_anywhere: true`, remember to turn on **Can Transform Anywhere** in
Dusklight's settings. You don't have to remember the others either: when you create your save, and
afterwards in the Archipelago window's Connection tab, the mod lists the Dusklight settings your
seed's logic expects.

---

## Generating and hosting the multiworld

One person (the host) collects everyone's options files, generates the multiworld, and hosts it.

The archipelago.gg website **cannot generate** games that use custom worlds like this one. Generate
on your own computer, then either upload the result to the website or host it yourself.

### Generate on your computer

1. Install Archipelago and this APWorld (see
   [Installing the APWorld](#installing-the-apworld-into-archipelago)). If other players use other
   custom worlds, install their APWorlds too.
2. Put every player's `.yaml` file in the `Players` folder of your Archipelago installation (on
   Windows usually `C:\ProgramData\Archipelago\Players`; the Launcher's **Browse Files** opens the
   Archipelago folder). Remove any files from other games you do not want in this multiworld. The
   `Templates` subfolder is ignored.
3. In the Launcher, press **Generate**.
4. When it finishes, the result is in the `output` folder, named like `AP_12345678901234567890.zip`.

If generation fails, the window shows why. See
[Generation errors](#generation-errors) for this game's messages.

### Host on archipelago.gg (recommended)

1. Go to [archipelago.gg/uploads](https://archipelago.gg/uploads).
2. Press **Upload File** and select the `AP_....zip` from your `output` folder.
3. On the seed page that opens, press **Create Room**.
4. Share the room page's link with everyone. It shows the server address with its port (for example
   `archipelago.gg:54321`) and every slot name.

Rooms on archipelago.gg go to sleep after a while without activity. Opening the room page in a
browser starts it again, and its port may change when it restarts.

### Host on your own computer

1. In the Launcher, press **Host** and select the `AP_....zip` (or the `.archipelago` file inside
   it).
2. The server listens on port `38281` by default.
3. Players on the same network connect to your computer's local IP address, for example
   `192.168.1.20:38281`. Players over the internet need your public IP address, and you must allow
   that port through your firewall and forward it on your router.
4. Keep the server window open while people play.

### What each player needs

| | Host | Twilight Princess Dusklight players | Players of other games |
|---|---|---|---|
| Archipelago + this APWorld | Yes, to generate | Only to make their YAML | No (only what their own game needs) |
| Dusklight + this mod + their own game copy | Only if also playing | Yes | No |
| Room address, slot name, password | Gives them out | Yes | Yes |

Everyone who plays this game must use the mod from the **same release** as the APWorld the host
generated with.

---

## Creating your Archipelago save in Dusklight

You create one save per multiworld. When you create it, the mod connects to the room, downloads
your slot, and **builds your game from the room's data** with the Dusklight Randomizer's own
generator. That is why the room must be running and reachable at that moment. After that, you can
play offline if you want.

1. Start Dusklight.
2. On the start screen, the first button shows the game mode. It says **Play** for the normal game.
   Use the arrows on either side of it (or left and right on your keyboard or controller) until it
   says **Archipelago**, then press it.
   - The game mode can only be changed on the start screen. While playing, use
     **Settings > Interface > Restart to Main Menu** in the Dusklight menu to get back there.
   - The title screen shows the *Twilight Princess Archipelago* logo while this mode is active.
3. On the file select screen, choose an **empty** file, as if you were starting a new game.
4. Instead of the name entry, an **Archipelago** window opens. Fill in:
   - **Server**: the room's address with its port, as shown on the room page, for example
     `archipelago.gg:54321`. The field starts with the last address you used (or
     `archipelago.gg:38281` the first time), so check the port.
   - **Slot Name**: your player name, exactly as in your YAML (case-sensitive).
   - **Password**: the room password. Leave it empty if the room has none.
5. Press **Connect**. The status line under the fields shows the progress:
   - `Connecting to archipelago.gg:54321...`
   - `Connected. Generating the seed...` followed by the generator's current step. This takes a few
     seconds.
   - `Ready! Seed ... for <your slot>.` in green, followed by `Press Start to name your file.`
   - If your seed's logic expects some Dusklight settings (see
     [Logic assumptions](#logic-assumptions-match-them-to-your-dusklight-settings)), they are listed
     under it in yellow, for example `Settings > Cheats > Can Transform Anywhere: On`. Turn them on in
     Dusklight's settings before you play.
   - If something goes wrong, the message appears in red. See
     [Troubleshooting](#troubleshooting-and-faq).
6. Press **Start**, then name your file as usual. Your adventure begins.

Closing the Archipelago window instead of pressing Start cancels and takes you back to the file
select screen.

Your save remembers the room: the server address, slot name and password are stored in it, and
loading the save later connects by itself.

---

## Playing

### Finding items

What you find in chests, on the ground, from bosses, characters, shops and so on depends on who the
item belongs to.

**Your own items** look and behave exactly like items in a normal Dusklight Randomizer seed,
including Foolish Items (traps) if your options added them.

<img src="images/ap_item.png" alt="The Archipelago item: six colored spheres on a ring" width="200" align="right">

**Another player's item** appears as the **Archipelago logo**: a spinning 3D model of six colored
spheres on a ring, like the logo's six circles. (If you prefer, the **Classic item model** switch in
the mod's entry of the Mods window shows these items as a paper note instead; it takes effect the
next time you load a save.) When you pick it up, the get-item text says whose item it is and what it
is, for example:

> You found Bob's Progressive Clawshot!

- The owner's name is in **yellow**.
- The item name is colored by how important it is to its owner: **purple** for progression items
  (needed to progress), **light blue** for useful items, **red** for traps, **green** for filler.
- Only progression items get the big item fanfare; the others get the short jingle.
- The text follows the game's language (English, German, French, Spanish, Italian or Japanese).

Shops and characters also name the other player's item when they offer it.

The item is sent to its owner as soon as you get it (or the next time you are connected).

### Receiving items

Items other players find for you go **straight into your inventory**, without a cutscene:

- They arrive as soon as Link is free: standing, walking or running, as Link or as the wolf, with no
  cutscene or conversation in progress. While you are in a cutscene, a conversation, swimming,
  climbing or riding, they wait.
- You must be connected to the room, playing the save you created for it.
- Progressive items (swords, bows, wallets, the Clawshot...) give you the next level, as in the
  randomizer.
- A **Received** notification says what arrived and who sent it, for example
  "Progressive Bow from Bob". When many items arrive at once (for example after you reconnect), they
  are grouped: "12 items, including ...".
- Items from your `start_inventory` arrive the same way, "from your starting inventory".

### Notifications

Small pop-ups appear in a corner of the screen:

| Title | When |
|---|---|
| **Received** | An item for you arrived in your inventory. |
| **Sent** | An item you found went to another player ("Moon Pearl to Alice"). |
| **Hint** | A hint about one of your items, or an item in your world, that has not been found yet. |
| **DeathLink** | Someone else died and took you with them. |
| **Archipelago** | Connection news ("Connected as ...", connection problems) and "Goal complete!". |
| (no title) | Chat messages and countdowns from the room. |

Item names use Archipelago's usual colors (purple for progression, blue for useful, red for traps).
You can turn notifications off with the **Notifications** switch, in the Archipelago window's
**Connection** tab or in the mod's entry in the Mods window.

### The Archipelago window

Open the **Dusklight menu** (press **F1** on a keyboard, the controller button Dusklight shows when
the game starts, or tap the screen with three fingers on a phone or tablet), then choose the
**Archipelago** tab. It has three tabs:

**Connection**

- The connection status, for example `Connected to wss://archipelago.gg:54321 as Link`, or
  `Reconnecting: ...` when the connection dropped.
- `Locations checked: X / Y` and `Items received: N`, and the Dusklight settings your seed's logic
  expects, if any.
- **Server**, **Slot Name** and **Password**, with **Connect** and **Disconnect** buttons. Use these
  if the room's address changed (for example a new port after an archipelago.gg room restarted):
  fix the address and press Connect. The new settings are stored in your save the next time the game
  saves.
- **DeathLink**: turn DeathLink on or off for this save.
- **Notifications**: turn the pop-ups on or off.

**Messages**

- Everything the room says, newest first: items found by everyone, hints, chat.
- A **Say** field to chat or send Archipelago commands, for example:
  - `!hint Progressive Clawshot` to ask where one of your items is.
  - `!remaining` to see how many of your items are still out there.
  - `!release` to give out everything left in your world (usually once you are done).

Hints about your items also show up here. Since the randomizer's hint signs are off, `!hint` is the
way to get hints in this game.

**Locations**

- The locations of your world that are not checked yet, and how many are left. Useful near the end
  to find what you missed.

### Playing offline and reconnecting

- Loading your save connects to the room by itself. If the room cannot be reached, the mod keeps
  retrying in the background, waiting longer between tries.
- You can play while disconnected. The locations you check are remembered and **sent the next time
  you connect**, and items other players sent you arrive then.
- If the connection drops while playing, the mod reconnects by itself when it can.
- To stop sending and receiving on purpose, press **Disconnect** in the Connection tab; press
  **Connect** to resume.

### DeathLink

If `death_link: true` was in your options, DeathLink starts on for your save. You can switch it in
the Connection tab at any time (the choice is stored in your save).

- When Link dies, everyone else in the room with DeathLink dies too. A fairy that saves Link does
  not count as a death.
- When someone else with DeathLink dies, Link dies too and a **DeathLink** notification says why.
  A fairy in a bottle saves you as usual.
- A death caused by someone else's DeathLink is not sent back to the room, so deaths do not bounce
  back and forth.

### The goal

Your goal is complete when you land the **final blow on Ganondorf**. The mod shows
**Goal complete!** and tells the room.

The game does not save after the credits, so the mod remembers the goal separately. If you were
offline when you beat Ganondorf, load your save and connect later: the goal is reported then.

---

## Moving a save to another device or reinstalling

Your Archipelago save has two parts:

- The **game save** itself, which includes this mod's save data: the room's address, your slot
  name, password, and how many items you already received.
- The **built seed**, which the mod keeps in its own data folder in Dusklight
  (`mod_data/io.github.pegzzzz.tp_archipelago`).

The built seed does not need to be copied: the mod can get your slot back from the room and rebuild
it. The game save must be copied **with its mod data**.

### Copy the save

1. On the old device, go to Dusklight's start screen and open **Settings**. In the **Prelaunch**
   tab, press **Open Save Manager**.
2. Select your Archipelago save, press **Export Save...** and choose
   **Save + mod data (.dusksave)**. (A plain `.gci` save loses the Archipelago data.)
3. Install Dusklight and the mod on the new device (same mod release).
4. On the new device's start screen, open **Settings > Prelaunch > Open Save Manager**, press
   **Import Save**, and pick the `.dusksave` file. On a computer you can also drag the file onto the
   Dusklight window while on the start screen.

### Load it the first time

1. Make sure the room is running.
2. Switch the start screen to **Archipelago** and load the save.
3. The mod says the seed data is missing on this device and will be restored from the room. It
   connects, then shows: "This save's seed was missing and has been restored from the room. Return
   to the title screen and load the save again to play it."
4. Do not play yet. Open the Dusklight menu, choose **Reset** and confirm (this returns to
   Dusklight's start screen), switch to **Archipelago** again, and load the save. The mod rebuilds
   your seed (a few seconds), and you can continue.

The same happens if you reinstall Dusklight or lose the mod's data folder on the same device.

Do not delete this mod's data (in the Save Manager or in the `mod_data` folder) while a multiworld is
in progress.

---

## Updating the mod and the APWorld

**Keep the mod and the APWorld matched.** The mod reads the data the APWorld put into the
multiworld, so both must come from the same release. The versions are shown:

- for the mod, in Dusklight's Mods window (for example `v0.1.0`);
- for the APWorld, in the list Archipelago prints when it generates (for example
  `Twilight Princess Dusklight: v0.1.0`).

### Updating the mod

- Drag the new `tp_archipelago.dusk` onto the Dusklight window (the install window says
  "Update from ..."), or replace the file in the `mods` folder and restart Dusklight.
- **During a multiworld, keep using the mod release that matches the APWorld the host generated
  with**, unless the release notes say otherwise. A mod that reads a different version of the room's
  data refuses it with:
  "This seed was generated with a different version of the Twilight Princess Dusklight APWorld
  (slot data version ..., this mod reads version ...). Use matching versions of the mod and the
  APWorld."
- When a compatible mod update changes how seeds are built, the mod rebuilds your seed by itself the
  next time you load your save.

### Updating the APWorld

- In the Launcher, use **Install APWorld** again with the new file (or replace it in
  `custom_worlds`), then restart the Launcher.
- Tell the Twilight Princess Dusklight players which release you generated with.

### Updating Dusklight

The mod is built for Dusklight 2.0. If a Dusklight update makes the mod show **Failed** in the Mods
window, check the releases page for a newer mod.

---

## Troubleshooting and FAQ

### Cannot connect

- **Check the address and port** on the room page. archipelago.gg rooms change port when they
  restart.
- **Address formats** the mod accepts:
  - `archipelago.gg:54321` (host and port);
  - `localhost` or `192.168.1.20` (no port: it defaults to `38281`);
  - `/connect archipelago.gg:54321` (pasted from a command; the `/connect` part is ignored);
  - `archipelago://archipelago.gg:54321`;
  - `wss://archipelago.gg:54321` or `ws://192.168.1.20:38281`.
- **Secure and plain connections:** without `wss://` or `ws://`, the mod tries a secure connection
  (`wss://`) first and then a plain one (`ws://`); for `localhost` it tries plain first. Writing
  `wss://` or `ws://` in front makes it use only that one. archipelago.gg uses secure connections;
  a server on your own computer usually uses plain ones.
- **The room is asleep** (archipelago.gg): open the room page in a browser to start it again, then
  check its port.
- **Hosting yourself:** make sure the server window is open, the port (default `38281`) is allowed
  through the firewall and, for players outside your home network, forwarded on your router.
- **Messages you might see:**
  - `This room has no player named "...".` The slot name is wrong (it is case-sensitive).
  - `Player "..." is not playing Twilight Princess Dusklight.` That slot belongs to another game.
  - `This room needs a password.` or `Wrong password.`
  - `"..." is not a valid server address.` Check for typos and extra characters.
  - `This server needs a newer version of the mod.`

### "This room is not the one this save was created for"

Each save belongs to one multiworld. You loaded a save made for another room, or you connected this
save to the wrong address. Fix the address in the Connection tab, or create a new save for this
room.

### "A different version of the Twilight Princess Dusklight APWorld"

The mod and the APWorld the multiworld was made with do not match. Ask the host which release they
generated with and install the mod from that same release. You see this when creating a save, and
also when loading an existing save after updating the mod to a release that reads a different
version: go back to the release you created the save with. If creating the save fails with
"Could not generate the seed: ...", the message also names both versions when they differ.

### "This save's seed data is missing on this device"

The mod's copy of your slot is not on this device (new device, reinstall, or deleted data). Connect
to the room: the mod downloads it again. Then reset to the start screen and load the save again.
See [Moving a save](#moving-a-save-to-another-device-or-reinstalling).

### Items are not arriving

- Check the Connection tab: you must be **Connected**.
- Items arrive only during normal gameplay: Link standing, walking or running (human or wolf), with
  no cutscene, conversation, menu, swimming, climbing or riding in progress. Walk around for a moment.
- Received items are counted in `Items received` in the Connection tab.

### Generation errors

These come from Archipelago when the host generates:

- **"... options make the game impossible to beat (for example more hearts or Poe Souls required
  than exist)"**: lower the counts in the access requirements, or shuffle the locations those items
  come from.
- **"with these options some ... locations can never be reached ... Change the options or use
  accessibility: minimal"**: change the options listed, or set `accessibility: minimal`.
- **"not enough Twilight Princess Dusklight locations for its progression items; shuffle more kinds
  of locations"**: turn on more shuffles (Golden Bugs, NPC gifts, shops, Poe Souls...).
- **"not enough dungeon reward locations for the Fused Shadows and Mirror Shards (are some of them
  excluded?)"**: remove dungeon reward locations from `exclude_locations`, or set
  `dungeon_rewards_can_be_anywhere: true`.
- **"could not place ..." / "no room left for ..."**: the key or map placement you chose has no room
  left, often because of excluded locations. Choose a wider placement (for example `any_dungeon`
  instead of `own_dungeon`).
- **Archipelago fill errors**: Archipelago's own options can make a multiworld impossible, for
  example a long `non_local_items` list when the other games do not have enough room for those items.
  Shorten the list.
- **Priority locations in barren dungeons:** with `unrequired_dungeons_are_barren: true`, a location
  in (or behind) a dungeon you do not need cannot hold progression items, so it cannot be a priority
  location. Generation does not fail: it prints a warning and treats that location as a normal one.

If a spoiler log was generated, it lists the required and barren dungeons of each Twilight Princess
Dusklight player.

### The mod shows Failed in the Mods window

- Make sure you have Dusklight 2.0 or newer, and the latest release of the mod.
- On iOS this mod cannot be loaded (see [iOS](#ios)).
- Select the mod and press **Logs** to see why.

### Where are the logs?

- **Dusklight:** open the Mods window, select **Twilight Princess Archipelago**, and press **Logs**.
  Press **Copy** to copy them. Most of the mod's lines start with `Archipelago:`. On a computer,
  Dusklight also keeps log files in its data folder (**Settings > Interface > Open Data Folder**).
- **Archipelago (generation and hosting):** the `logs` folder of your Archipelago installation
  (**Browse Files** in the Launcher).

### Reporting a bug

Open an issue on the project's
[GitHub issues page](https://github.com/Pegzzzz/Dusklight-Archipelago/issues). Please include:

- the mod and APWorld versions, your Dusklight version, and your platform;
- what you did, what you expected, and what happened;
- the mod's logs from Dusklight (copied as above);
- for generation problems, your YAML file and the Archipelago log.

### Other questions

**Can I play the normal game or the Randomizer with this mod installed?** Yes. Pick their game mode
on the start screen. Their saves are separate.

**Do I need to be online the whole time?** Only to create the save. After that you can play
offline; everything is sent when you reconnect.

**Can I get in-game hints?** The randomizer's hint signs and Agitha's hints are off in this mode.
Use `!hint` in the Messages tab.

**Is entrance randomization supported?** Not yet.

---

## Credits

- **[Dusklight](https://github.com/TwilitRealm/dusklight)** and the
  **[Dusklight Randomizer](https://github.com/TwilitRealm/dusklight-randomizer)** by Twilit Realm.
  This project is a fork of the randomizer; everything it does in game rests on their work. The
  *Twilight Princess Archipelago* title logo comes from the randomizer's own `archipelago` branch.
- **[Archipelago](https://github.com/ArchipelagoMW/Archipelago)** and its community.
- The Archipelago integration (mod and APWorld) by Elliot:
  [Pegzzzz/Dusklight-Archipelago](https://github.com/Pegzzzz/Dusklight-Archipelago).
