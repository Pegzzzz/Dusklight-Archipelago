# Twilight Princess Dusklight

## What is this game?

*The Legend of Zelda: Twilight Princess*, played on [Dusklight](https://twilitrealm.dev), the PC (and
mobile) reimplementation of the game. Link must save Hyrule from the Twilight Realm with Midna's
help, as a hero and as a wolf.

The Archipelago support is a Dusklight mod built on the official Dusklight Randomizer: the game is
patched the same way as a randomizer seed (opened areas, cutscene skips, item behaviour, logic), but
the items come from the whole multiworld. It is its own game mode with its own save files, so it can
be installed next to the randomizer and other Dusklight mods.

## Where is the options page?

The [player options page for this game](../player-options) contains all the options you need to
configure and export a config file.

## What does randomization do to this game?

Every location the randomizer can shuffle is an Archipelago location: chests, items lying around,
heart containers from bosses, dungeon rewards, gifts from characters, and, depending on your
options, Golden Bugs, Ancient Sky Book characters, shop items, Hidden Skills, Poe Souls and hidden or
freestanding rupees. Items from your world can be found by other players and theirs can be in your
world.

The logic is the randomizer's own logic, including Link's form (human or wolf), the time of day and
the twilight areas. The game is always beatable with the options you chose.

## What is the goal?

Defeat Ganondorf. The options decide what opens the way: the Hyrule Castle barrier, the Hyrule
Castle big key and the Palace of Twilight can each require Fused Shadows, Mirror Shards, completed
dungeons, Poe Souls or hearts.

## Which items can be in another player's world?

Any of your items, depending on the options: equipment, heart pieces and containers, bottles, Fused
Shadows and Mirror Shards, warp portals, Hidden Skills, Golden Bugs, Poe Souls, rupees and, if you
allow it, your dungeon keys, maps and compasses.

## What does another world's item look like?

A scroll, like the Hidden Skill letters. Opening a chest or picking it up shows whose item it is and
what it is, for example "You found Bob's Moon Pearl!", coloured like Archipelago does (progression
items in purple, useful ones in blue, traps in red). Shops and characters also name the other
player's item.

## When the player receives an item, what happens?

It goes straight into your inventory as soon as Link is free to move, and a notification says what
arrived and who sent it. Progressive items (swords, bows, wallets...) give the next level, as in the
randomizer.

## Is DeathLink supported?

Yes. When Link dies (a fairy that saves him does not count), everyone with DeathLink dies, and the
other way around. It can be turned on in your options and toggled in game.

## Are there in-game hints?

The randomizer's hint signs are turned off because they would describe other players' items. Use
Archipelago's `!hint` command instead: the in-game Archipelago menu has a field for chat and
commands.

## What is not supported yet?

- Entrance randomization.
- The randomizer's hint options and Agitha's hints.
- The randomizer's own plandomizer (Archipelago's `plando_items` works).
