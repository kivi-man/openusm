# OpenUSM

An open-source reimplementation layer for Ultimate Spider-Man (PC, 2005) by Treyarch.
Requires a legitimate copy of the game. No game files are included or distributed.

Originally forked from [LemonHaze420/openusm](https://github.com/LemonHaze420/openusm).

---

## Features

### Rendering
- Comic panel freeze-frame textures rendered at native screen resolution (previously hardcoded 256x256)
- Subtitle text rendered on the topmost layer, above all comic panels and UI elements

### City and World
- Terrain streaming distance extended to 2,500 m
- Camera far clip plane extended to 4,500 m
- Character and prop visibility extended to 500 m (no more pop-in on wide shots)
- HD building geometry rendered up to 3,500 m
- Modular building details (rooftops, fire escapes, fences) rendered up to 3,000 m
- City LOD strip evaluation extended to match the far clip plane

### Subtitles
- In-game voice subtitle system activated via the -subtitles command-line flag
- Transcripts loaded from udio_transcripts.json placed in the game folder
- Ambient NPC voices automatically filtered - only story and character dialogue is shown
- Named character voices (Spider-Man, Johnny Storm, Venom, etc.) are always prioritized
- DS4 rumble and timing adjusted per speaker priority

### Controllers
- DualShock 4 / PS4 controller support with full button mapping and rumble

### Stability
- Heap crash guard: intercepts null and invalid pointer frees that caused recurring crashes in the original engine
- Scene animation crash fix: experimental animation branch replaced with the stable native implementation

### Mod Support
Place files in a mods/ folder next to the game executable.

| Extension | Type |
|-----------|------|
| .dds, .tga | Texture replacement |
| .obj, .fbx, .dae, .gltf | Mesh replacement |
| .pcmesh | Native mesh file replacement |
| .fdf | Font file override |

Files are matched to game assets by filename hash. No pak repacking required.

---

## Requirements

- Ultimate Spider-Man (PC) - original copy required
- Windows 10 or later (64-bit host, 32-bit game process)
- To build: WSL2 with Ubuntu, MinGW-w64 i686 cross-compiler

---

## Installation

1. Download inkw32.dll from the [Releases](../../releases) page.
2. Rename the original inkw32.dll in your game folder to inkw32_.dll.
3. Place the downloaded inkw32.dll in the game folder.
4. Copy the shaders/ directory from this repository into the game folder.

To enable subtitles:
1. Download udio_transcripts.json from the [Releases](../../releases) page.
2. Place it in the game folder (next to USM.exe).
3. Launch the game with the -subtitles flag:

`
"C:\path\to\USM.exe" -subtitles
`

Or create a shortcut and add -subtitles to the target field.

---

## Building from Source

`ash
# Clone
git clone https://github.com/yourusername/openusm.git
cd openusm

# Configure
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw32.cmake

# Build
wsl -d Ubuntu make -C /mnt/path/to/openusm/build -j8 binkw32
`

Output: uild/binkw32.dll

---

## Mods Folder

Create a mods/ folder next to USM.exe and place supported files inside.
The engine loads them at startup. Texture and mesh replacements take effect immediately on the next level load.

Font overrides (.fdf) replace the game's built-in font definitions. The subtitle system uses i_upupandaway.fdf by default.

---

## Credits

- [LemonHaze420](https://github.com/LemonHaze420) - original openusm project and engine reverse engineering
- All original openusm contributors

---

## License

Source code is provided for educational, documentation, and modding purposes only.
Commercial use is not permitted. Derivative works must remain open source and credit the original authors.
Game assets, audio, and trademarks belong to their respective owners (Treyarch, Activision, Marvel).
