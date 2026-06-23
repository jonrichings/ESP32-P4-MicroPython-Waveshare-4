# Resume Notes: Workspace Migration & Final Verification

This note is for the AI assistant to resume progress after renaming the repository folder to **`ESP32-P4 MicroPython`**.

---

## 1. What We Just Accomplished
* **Camera Fix**: Patched `mod_camera.c` to override the default Bayer type for the **OV5647** sensor to `3` (BGGR) instead of the driver-reported GBRG.
* **Verification**: Rebuilt and flashed the firmware to `COM5`. Confirmed the camera preview now renders correct, vibrant, full colors and upright orientation at **~33 FPS** with `hmirror=0, vflip=0`.

---

## 2. Migration To-Do List
Once the folder is renamed to `ESP32-P4 MicroPython`, we need to perform these three steps:

### Task A: Update VS Code Settings
Amend `.vscode/settings.json` to replace `CellaVision_HMI_3` with `ESP32-P4 MicroPython` on these lines:
1. `"idf.projectPath"`
2. `"cmake.sourceDirectory"`
3. `"clangd.arguments"` (the `--compile-commands-dir` argument)

### Task B: Rename Local C-Drive Folders & Reconfigure
1. Rename local compiler folders on the `C:` drive:
   * Source: `C:\esp-build\CellaVision_HMI_3_src` -> `C:\esp-build\ESP32-P4_MicroPython_src`
   * Build: `C:\esp-build\CellaVision_HMI_3_build` -> `C:\esp-build\ESP32-P4_MicroPython_build`
2. Run the clean reconfiguration command:
   ```powershell
   $env:PYTHONIOENCODING = "utf-8"; chcp 65001; $env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.3.3"; . C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1; idf.py -C C:\esp-build\ESP32-P4_MicroPython_src\micropython\micropython\ports\esp32 -B C:\esp-build\ESP32-P4_MicroPython_build reconfigure
   ```

### Task C: Update Markdown Document Links
Search and replace all instances of `CellaVision_HMI_3` with `ESP32-P4 MicroPython` in:
* `walkthrough.md`
* `esp_idf_setup_guide.md`
* `p4_stabilization_guide.md`
