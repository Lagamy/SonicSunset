# SonicSunset (SonicFantasy)

C++ reimplementation of the behavior of Sonic Adventure and Shadow the Hedgehog (2005). Docs: `docs/ARCHITECTURE.md`.

## The engine

The engine is **IluminationEngine**, a separate project with its own repo (https://github.com/Lagamy/IlluminationEngine) that this game and DevilMayCry Rebirth share. The GitHub repo is spelled with two l's, the folder next to this one with one, so clone it under the folder name the symlink expects:

```
git clone git@github.com:Lagamy/IlluminationEngine.git IluminationEngine
```

Layout:

```
Github/
  IluminationEngine/
  SonicFantasy/        <- this repo; IluminationEngine -> ../IluminationEngine is a committed symlink
```

The game's `CMakeLists.txt` adds it with `add_subdirectory(IluminationEngine ${CMAKE_BINARY_DIR}/IluminationEngine)` and links the static library. On Windows the symlink needs `git config core.symlinks true` and developer mode (or recreate it with `mklink /D IluminationEngine ..\IluminationEngine`). What the engine expects from a game is in the engine's README.
