Multiple Configuration Files
============================

MAME has a very powerful configuration file system that can allow you to tweak settings on a per-game, per-system, or even per-monitor type basis, but requires careful thought about how you arrange your configs.

.. _advanced-multi-CFG:

Order of Config Loading
-----------------------

1. The command line is parsed first, and any settings passed that way *will take
   precedence over anything in an INI file*.

2. ``mame.conf`` (or other platform INI; e.g. ``mess.conf``) is parsed twice.  The
   first pass may change various path settings, so the second pass is done to
   see if there is a valid configuration file at that new location (and if so,
   change settings using that file).

3. ``debug.conf`` if the debugger is enabled.  This is an advanced config file,
   most people won't need to use it or be concerned by it.

4. Screen orientation INI file (either ``horizont.conf`` or ``vertical.conf``).
   For example Pac-Man has a vertical screen, so it loads ``vertical.conf``,
   while Street Fighter Alpha uses a horizontal screen, so it loads
   ``horizont.conf``.

   Systems with no monitors, multiple monitors with different orientations, or
   monitors connected to slot devices will usually load ``horizont.conf``.

5. Monitor type INI file (``vector.conf`` for vector monitors, ``raster.conf`` for
   CRT raster monitors, or ``lcd.conf`` for LCD/EL/plasma matrix monitors).
   Pac-Man and Street Fighter Alpha use raster CRTs, so ``raster.conf`` is loaded
   here, while Tempest uses a vector monitor, so ``vector.conf`` is loaded here.

   For systems that have multiple monitor types, such as House Mannequin with
   its CRT raster monitor and dual LCD matrix monitors, the INI file relevant to
   the first monitor is used (``raster.conf`` in this case).  Systems without
   monitors or with other kinds of monitors will not load an INI file for this
   step.

6. Driver source file INI file.  MAME will attempt to load
   ``source/``\ *<sourcefile>*\ ``.conf`` where *<sourcefile>* is the base name
   of the source code file where the system driver is defined.  A system's
   source file can be found using **mame -listsource <pattern>** at the command
   line.

   For instance, Banpresto's Sailor Moon, Atlus's Dodonpachi, and Nihon System's
   Dangun Feveron all run on similar hardware and are defined in the
   ``cave.cpp`` source file, so they will all load ``source/cave.conf`` at this
   step.

7. BIOS set INI file (if applicable).  For example The Last Soldier uses the
   Neo-Geo MVS BIOS, so it will load ``neogeo.conf``.  Systems that don't use a
   BIOS set won't load an INI file for this step.

8. Parent system INI file.  For example The Last Soldier is a clone of The Last
   Blade / Bakumatsu Roman - Gekka no Kenshi, so it will load ``lastblad.conf``.
   Parent systems will not load an INI file for this step.

9. System INI file.  Using the previous example, The Last Soldier will load
   ``lastsold.conf``.


Examples of Config Loading Order
--------------------------------

* Brix, which is a clone of Zzyzzyxx. (**mame brix**)

  1. Command line
  2. ``mame.conf`` (global)
  3. (debugger not enabled, no extra INI file loaded)
  4. ``vertical.conf`` (screen orientation)
  5. ``raster.conf`` (monitor type)
  6. ``source/jack.conf`` (driver source file)
  7. (no BIOS set)
  8. ``zzyzzyxx.conf`` (parent system)
  9. ``brix.conf`` (system)

* Super Street Fighter 2 Turbo (**mame ssf2t**)

  1. Command line
  2. ``mame.conf`` (global)
  3. (debugger not enabled, no extra INI file loaded)
  4. ``horizont.conf`` (screen orientation)
  5. ``raster.conf`` (monitor type)
  6. ``source/cps2.conf`` (driver source file)
  7. (no BIOS set)
  8. (no parent system)
  9. ``ssf2t.conf`` (system)

* Final Arch (**mame finlarch**)

  1. Command line
  2. ``mame.conf`` (global)
  3. (debugger not enabled, no extra INI file loaded)
  4. ``horizont.conf`` (screen orientation)
  5. ``raster.conf`` (monitor type)
  6. ``source/stv.conf`` (driver source file)
  7. ``stvbios.conf`` (BIOS set)
  8. ``smleague.conf`` (parent system)
  9. ``finlarch.conf`` (system)

*Remember command line parameters take precedence over all else!*


Tricks to Make Life Easier
--------------------------

Some users may have a wall-mounted or otherwise rotatable monitor, and may wish
to actually play vertical games with the rotated display.  The easiest way to
accomplish this is to put your rotation modifiers into ``vertical.conf``, where
they will only affect vertical games.
