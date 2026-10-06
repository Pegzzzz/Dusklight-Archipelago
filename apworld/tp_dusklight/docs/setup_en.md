# Twilight Princess Dusklight: Multiworld Setup Guide

## Required software

- [Dusklight](https://twilitrealm.dev/install/) 2.0 or newer, with your own copy of Twilight
  Princess (Dusklight does not include the game).
- The **Twilight Princess Archipelago** mod (`tp_archipelago.dusk`) and the **APWorld**
  (`tp_dusklight.apworld`), both from the same release on
  [the project's releases page](https://github.com/Pegzzzz/Dusklight-Archipelago/releases).
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.4 or newer, to generate
  and host multiworlds.

## Installing

### The mod

Copy `tp_archipelago.dusk` into Dusklight's `mods` folder:

- Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
- Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
- macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`

Start Dusklight and check that **Twilight Princess Archipelago** is enabled in the Mods window. It
adds an **Archipelago** game mode with its own save files; the Dusklight Randomizer and your other
mods keep working.

### The APWorld

Open `tp_dusklight.apworld` with the Archipelago Launcher (or copy it into the `custom_worlds`
folder of your Archipelago installation). The person generating the multiworld needs it; players
can use it to make their options file.

## Creating your options file

In the Archipelago Launcher, use **Generate Template Options** and edit
`Twilight Princess Dusklight.yaml`, or start from this example:

```yaml
name: YourName
game: Twilight Princess Dusklight
Twilight Princess Dusklight:
  skip_prologue: true
  faron_twilight_cleared: true
  hyrule_barrier_requirements: fused_shadows
  hyrule_barrier_fused_shadows: 3
  small_keys: own_dungeon
  golden_bugs: true
  death_link: false
```

Every option is described in the template. Options that make the game impossible to beat (for
example more hearts required than exist) are refused when the multiworld is generated.

## Generating and hosting

Put everyone's options files in Archipelago's `Players` folder and generate as usual (see
[Archipelago's setup guide](https://archipelago.gg/tutorial/Archipelago/setup/en)). Upload the
result to [archipelago.gg](https://archipelago.gg/uploads) or host it yourself.

## Connecting

1. In Dusklight, select **Archipelago** in the Dusklight menu, then create a new save file.
2. Enter the room's address (for example `archipelago.gg:38281`), your slot name and the password if
   the room has one, then press **Connect**.
3. The mod downloads your slot from the room and builds your world, which takes a few seconds. When
   it says **Ready**, press **Start** and name your file.

Your save file remembers the room: loading it later reconnects by itself. If the room's port
changed (archipelago.gg rooms get a new port when they restart), open the **Archipelago** tab of the
pause menu, fix the address and press **Connect**. The new address is kept in your save.

You can also play offline: the locations you check are sent the next time you connect, and the
items others sent you arrive then.

## In game

The **Archipelago** tab of the pause menu has:

- **Connection**: the status, how many locations you checked and items you received, the room's
  address, DeathLink and notifications.
- **Messages**: the room's messages (items found, hints, chat) and a field to chat or use commands
  such as `!hint Progressive Clawshot`, `!remaining` or `!release`.
- **Locations**: the locations of your world that are not checked yet.

## Troubleshooting

- **"a different version of the Twilight Princess Dusklight APWorld"**: the mod and the APWorld must come from
  the same release. Ask the person who generated the multiworld which version they used.
- **"This room is not the one this save was created for"**: this file belongs to another
  multiworld. Create a new save for this room.
- **Cannot connect**: check the address and port on the room page. Secure (`wss://`) and plain
  (`ws://`) connections are both tried; write the address with `wss://` or `ws://` to use only one.
- **Seed data missing**: the mod keeps a copy of your slot in Dusklight's settings folder
  (`mod_data/io.github.pegzzzz.tp_archipelago/archipelago`) and rebuilds your world from it when
  needed. Don't delete it while a multiworld is in progress.
