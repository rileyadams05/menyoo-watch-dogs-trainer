Watch Dogs Deluxe Edition v1.06.329 (incl DLC) - Uplay

- This version makes use of features introduced in CE v7.1x or higher...
- Best practise is to load the table AFTER your save game has fully loaded...
- You should only work with 'normal color' cheats (do not touch 'greyed' info)
- When returning to the main game menu, best is to disable the main script first!
- FastTravel (and Cutscenes) will 'reset' data structure pointers. Whenever this happens,
  you must re-enable selected (sub)options again, if needed...
- All values (pointers) will be collected while "active" as player.
  (iow not while in 'ESC' or 'TAB' mode)
- Also - where appropriate - 'greyed' structure pointers are shown, and these must contain a value to work properly !
  (if zero, switch back ingame to have them collected)
- If you initially selected an incorrect game_exe, you might be prompted with a warning upon selected the correct exe.
  You can safely ignore this message, and the table will load fine thereafter...
- After returning back to the Title Screen, you should disable all cheats (using [Ctrl+F12]) !
  (or else there is a good chance of crashing the game upon returning ingame...)
- Always make manual backups of your save(s) to avoid possible loss (~ corruption/locked player/etc)
  You have sole/full responsibility over your gaming environment.


Prenotes:
1. every teleport action will now automatically enable 'God Mode'...
   (so do not forget to disable it afterwards)
2. Hotkeys have been changed to use [Shift] (instead of [Ctrl], as this is used ingame a lot)
3. this game makes use of a '1-file' save_system; therefore make manual backups regularly !
4. upon loading the game's Main Menu, it will first try to connect to the servers (which can take quite some time)
   Disabling your network connection temporarily will skip this attempt instantly
   (you can use the [_DisableNet.bat] to dis/connect with ease ~ edit batchfile to fill in your network's LAN name)
5. Swapping to Desktop can cause mouse_control issues: either right-click to or press [Ctrl+Alt+Del + mouse_click]
   to regain mouse-control
6. Tip: to speed up 'End_credits', use CE's Speedhack...


* Player:
- Health: can be locked if needed
- Godmode [Shift+G]: use(d) especially during Teleporting... (do not forget to disable it)
  Note: by default, also NPCs will no longer die (and your 'Reputation' should remain untouched...)
        (see 'pChainHealth (greyed) to disable this feature)
- Invisible: you will become undetectable
  (does not apply during certain (sub)mission events...)
- Car Damage:
  > Health Car: can be locked/edited if needed
    > Defensive Driver: (~skill) reduced damage from collisions (0 = no damage - does not apply to spiked tires)
    > Offensive Driver: (~skill) increased damage by collisions (>30-100 = instant destruction for most vehicles)
      (~ pretty much identical effect as with '1-Hit Destruction')
    Notes:
    a. visual damage will continue to degrade, but will/should have no influence on you riding it...
    b. does not apply to tires; driving over elevated spikes will destroy them...
  > 1-Hit Destruction: reduces any hit car's health instantly; any next "major" hit will destroy it
    (this "reduced" value can be set via 'pChainHealth (greyed) ~ 'Max Car Health')
- Wheapon Wheel:
  > Lock Ammo (No Reload): will no longer consume ammo (applies to any weapon/ammo_type)
  > Lock Craft Materials: will no longer consume materials
  > Refill Items: any item in the wheel will be automatically set to a 'minimum' value
    (this 'minimum' value can be set via 'pChainHealth (greyed) ~ 'Max Quantity')
- Focus:
  > Infinite Focus: locks focus amount
  > Focus (default >= 1): you can set the amount of focus here
  > Slowed Focus (0~0.01): lower is slower, but '0.01' pretty much freezes everybody/thing...
- Battery:
  > Maximum Slots: can be locked if needed
  > Refill Multiplier (0 = Instant - Default = 1): any value between 0 and 1
- Police Alerts:
  > Heat Level: normal (white indicator) police alertness; can be locked
  > Reset Police Radar: stops (yellow) police radar from scanning you altogether
    (some mission_events DO expect you to be detected though !)

- Teleport & Coordinates:
> Teleport to Waypoint: (you must enable this script first before setting any waypoint

When selecting any location on the map (incl icons), teleporting to that location should be fine.
However, keep in mind that Player can sometimes drop from a greater height (and therefore will/could die/desync).
To avoid this altogether, simply enable 'God mode' first (which is normally done automatically)...

1. Open the map and select a(ny) location
   (disable the/any waypoint on the map first, if needed)
2. Press [Shift+T] TWICE while still in the map view...!
3. Press [ESC] and Player should now teleport to that particular location

Notes:
1. do not choose buildings/rooftops and/or icons "leaning" on/against buildings
2. occasionally you will drop through the surface. Quickly press [Esc] and then manually change your
   current 'Player: Zval' (see 'Coordinates Set') to a higher value ('80' seems to work just fine in most cases).
   Press [Esc] to return ingame...
3. occasionally you will get stuck inside a structure. In these cases, enable 'Free Roam' to move out of that location.
4. if you do not seem to teleport at all (even though coordinates are collected), then simply dis/re-enable the script...
5. avoid teleporting while ingame; should work fine but who knows...
6. in some rare occassions, you'll wind up stuck deep below/above surface. Either reload latest 'Autosave';
   and perhaps even kill/restart game...

> Free Roam (also check out Help function)
Once enabled, you use your mouse to steer in a particular direction, and use Numkeys to move about.

Notes:
1. upon enabling free roam, you must FIRST move forward a bit in order for the game to pick up...
(in some cases, you'll need to push both keys - eg forward and NumKey '8' - at the same time for a moment)
2. avoid moving through structures, as this can "confuse" the engine
3. you can hold a key to speed up things (you could also change a 'stepValue' accordingly ~ see [Script values])
4. you can end the roaming-session either using [F10] (or disabling the script) or [F12]
(which should bring you back to your start position). In the lather case - when unsuccessful, enable 'Save & Restore Coordinates'
and press [F12] again...
5. your player can change of posture depending on when/where you start roaming; or because of passing through certain structures.

> Save & Restore Coordinates: use [F11] (save current coordinates) and [F12] (restore saved coordinates)
  to teleport back to a specific location.
  Notes:
  1. you have the option to either reset coordinates after teleporting (to avoid inadverted teleporting); or just keep them active.
  2. you 'enable' this feature by selecting the appropriate option_choice in the 'Value' column...
  3. the feature also takes 'Drop Height' into account (to avoid possible collisions)


- Miscellaneous:
  > Timers: most of them are taken into account (but not all of them). 
    Certain sidemissions (such as Fixer, etc) can/could be using different timers depending the type of contract...
  > Player Status: following info can be changed/updated:
    - Money *
    - Experience * ('Level' is informational)
    - Notoriety
    - Skills (incl Mini Games)
    - Reputation
    Note: while testing with some (downloaded) 'Saves', these (*) 2 amounts switched places...
  > Manage Progression Wheel: allows you to quickly unlock rewards in a(ny) progression tree
    (keep in mind that the game only unlocks a reward upon reaching its threshold)
    e.g.: 'Criminal Convoys ~ Destroyer' = finish 10 missions). There are 2 ways to unlock this reward earlier on:
    a. - Open the list, and choose 'Set reward threshold...' (red button at top-right window)
       - Select [All] to set all rewards accordingly
       - The list will now show at which (next) threshold this reward will be unlocked
    b. - Open the list, and choose 'Edit reward...' ("folder"-button at top-right window ~ opens a new window)
       - Set 'Actual' to its reward's threshold, minus '1' ! (for the Destroyer, this would mean '9')
    => upon finishing the mission successfully, you should get the "next reward" in line...
    Important: the original reward_thresholds will ONLY be restored by restarting the game !
  > Time Of Day: set/lock time of day.
    To set a particular daytime, fill in a (decimal) hour_time; and then run/click 'Set Time...'.
    (current time is shown greyed)
  > Poker Hand Money: see 'Mini Game Tips'
  > Set Speed: see 'Mini Game Tips' on when to use it (for example); makes use of CE's Speedhack
  > Cam Distance: change the distance towards the player's location (not exactly FOV, as the wide_angle remains the same !)
    Notes:
    a. there seems to be a max_value for this setting; any higher value will have no effect
    b. you can/could also change the 'Multiplier' (~ lock value); with the same limitation though
  > Mini Game Tips: tips on how to handle these particular games...



* [Tools] menu:  (see top menu-bar choices)
> Compact View:
allows you to show/hide the typical CE interface (Compact (default) = show only the cheats-list)

> Select & Launch Your Game...: allows you to launch the game directly from this table.
The 1st time, you'll need to find/select your game's (sub)folder. Once the game has launched - and you want to keep its (sub)folder location - you can/must save the table manually to keep that setting permanently.
(see also 'Cheat table settings' below)

> Disable All Cheats: will disable all cheats, and unlock any locked values
  (you could use this between mission/save/etc-loads when crashes seem to occur at that moment in time)
  Note: [Ctrl+F11] = unlocks 'locked' values only ~ [Ctrl+F12] = disables all cheats (incl scripts)

> Show Table Name: will - in most cases - only show correct tablename when game has not yet been launched/attached.
  v7.1+ users will/should always get the correct tablename shown !

> Errors & Settings:
§ Error Statistics...: logs errors handled by CE (basically handling possible crashing);
                       and all values should remain at '0' normally

§ Cheat table settings...: (as user you can change certain - default - settings here)
- Browser: upon clicking the 'Info' button, this broswer will be launched with a search string
  to find the related game topic @FRF (it should use your default search engine)
- Compact View Mode: set your preferred default startup
- Color: will be used as background color in the cheat list overview
  (you can use one of the online 'Color picker' tools to get a proper (hex)value)
- Game' exe and (sub)folder: exe used for 'auto-attach'-ing; and opening the proper game folder via 'Select & Launch Your Game'
  Note 1: the 1st time that you use this feature, your game folder_location will be copied here. However, you must save
          the CE table manually in order to keep this setting permanently.
          Also: clear this info in case you have changed your game location thereafter... (or update it manually here)
  Note 2: some tables offer a feature to export/save certain reports to a file.
          In such cases, the "export function" will look for its 'Report export location' here.
          (if you get a '... is undefined' error message, you'll have an incorrect path reference filled in here !)
          * <game> : refers to your game location (entered in previous field above)
          * <table>: refers to your CE table location (see also: [Tools ~ Show Table Name...])
          * 'any existing folder' (eg: C:\TEMP\)

Note: use 'apply above changes...' to check out your changes first, before saving !

Important: if you want to keep any change(s) permanently, you must manually save the table, quit CE and load up the table again!

- Developer tools... (do not use, unless at your own risk)
  Important: this table depends on '(game verification...)' set properly. Iow do not change this setting !
- Maintenance scripts... (do not use, unless at your own risk)
  (allows table author to manage 'system' settings more easily ~ again, not to be used by gamer at all !)


ps: you can change/introduce hotkeys via CE's 'hotkey' feature
    (eg: [Shift+G] = dis/enable God Mode)
