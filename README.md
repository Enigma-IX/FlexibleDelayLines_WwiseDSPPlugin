# Flexible Delay Line – Wwise Plugin

## Overview

This repository contains a **Wwise audio plugin** implementing a **Flexible Delay Line**, inspired by the chapter *"Flexible Delay Lines"* written by **Robert Bantin** in *Game Audio Programming 4: Principles and Practices*.

The goal of this project is to reproduce the behavior described in the book:  
a flexible, continuously adjustable delay line suitable for real-time game audio use.

The plugin is built using the **official Audiokinetic Wwise plugin build system** (`wp.py`).

---

## Requirements

- **Wwise SDK installed**  
  Version used in this project: `2025.1.4.9062`
- **Python**
- **Visual Studio 2019/2022** (VC160/VC170 toolchain)

Official documentation:
- [https://www.audiokinetic.com/en/public-library/2025.1.4_9062](https://www.audiokinetic.com/en/public-library/2025.1.4_9062/?source=SDK&id=effectplugin_tools.html)

---

## Build Instructions

All commands must be run from the **root of the plugin repository**.

Documentation:
- https://www.audiokinetic.com/en/public-library/2025.1.4_9062/?source=SDK&id=effectplugin_tools_building.html

### 1. Generate project files (Premake)

Generate Visual Studio project files for the Wwise Authoring plugin:

```bash
python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" premake Authoring
```

---

### 2. Build the plugin

Build the Authoring plugin in Release / x64 configuration:

```bash
python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" build -c Release -x x64 -t vc170 Authoring
```

---

## Packaging (for Wwise Launcher)

Documentation:
- https://www.audiokinetic.com/en/public-library/2025.1.4_9062/?source=SDK&id=effectplugin_tools_packaging.html

### 1. Package the plugin

Create the `.tar.xz` package used by Wwise:

```bash
python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" package Authoring --version 2025.1.4.9062
```

⚠️ **Important:**  
The version passed to `--version` **must match the Wwise SDK version**.  
This is required for compatibility with **Wwise Launcher**.


---

### 2. Generate the plugin bundle

Generate a bundle that can be installed via **Wwise Launcher**:

```bash
python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" generate-bundle --version 2025.1.4.9062
```

---

## Using the Plugin in Wwise

0. Download the plugin (release section)
1. Open **Wwise Launcher**
2. Go to **Plug-ins → Manage Plug-ins**
3. Install the plugin from file
4. Launch **Wwise**
5. The *Flexible Delay Line* plugin should appear as an available effect

---

## References

- *Game Audio Programming 4: Principles and Practices*  
  Robert Bantin – Flexible Delay Line chapter  
  https://www.routledge.com/Game-Audio-Programming-4-Principles-and-Practices/Stevens-Raybould/p/book/9781032181286

- Audiokinetic Wwise SDK Documentation  
  https://www.audiokinetic.com/library/

---

## Notes

- The delay line is implemented as a **circular buffer** with independent read/write heads.
- Smooth delay-time changes should be interpolated to avoid audio artifacts.
- This structure is commonly used for delay, chorus, flanger, and time-based modulation effects.
