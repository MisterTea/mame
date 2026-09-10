# LaunchBox metadata

MAMEHub's machine and software pickers read the checked-in `launchbox/`
directory from MAME's configured **UI path**. Refresh it from the
public LaunchBox Games Database export with:

```sh
python3 scripts/mamehub/update_launchbox_metadata.py launchbox
```

The source archive is downloaded from
`http://gamesdb.launchbox-app.com/Metadata.zip`. Metadata is split into an
arcade database and one database per software platform so every file fits in
ordinary Git without Git LFS. Release packaging must place the directory next
to `ui.ini` (or in another directory in `ui_path`).

Arcade records are matched by MAME short name. Software records are matched by
normalized title, with the selected machine name used to choose the best
platform when LaunchBox has the same title on several platforms.
