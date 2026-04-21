# VS Code + Claude Code Setup Guide for the Solar Cooker Ray Tracer

Follow the steps in order. You'll install a C++ toolchain, CMake, vcpkg (for dependencies), Git, VS Code, the Claude Code extension, and the right VS Code extensions. Then you'll create the project skeleton, hand the plan document to Claude Code, and let it start building Phase 1.

**Total setup time:** ~45 minutes on a fast connection.

---

## Step 1 — Install prerequisites

Pick your OS section and run the commands in a terminal.

### Windows 10/11

1. **Install Visual Studio 2022 Community** (free) from <https://visualstudio.microsoft.com/downloads/>. During the installer, check the **"Desktop development with C++"** workload. Leave the defaults. This gives you MSVC, the Windows SDK, and a C++ debugger.
2. **Install Git for Windows** from <https://git-scm.com/download/win>. Accept the defaults.
3. **Install CMake** from <https://cmake.org/download/> → "Windows x64 Installer". During install, choose **"Add CMake to the system PATH for all users"**.
4. **Install Node.js LTS** (22.x or newer) from <https://nodejs.org/>. Needed for the Claude Code CLI.
5. **Install Python 3.11+** from <https://www.python.org/downloads/>. Needed for some vcpkg package build scripts. Check **"Add Python to PATH"** during install.
6. Reboot so every PATH update is picked up.

**Verify:** open PowerShell and run each of the following — every command should print a version:
```powershell
cl              # should show "Microsoft (R) C/C++ Optimizing Compiler"
cmake --version
git --version
node --version
python --version
```
If `cl` is not found, you need to open **"x64 Native Tools Command Prompt for VS 2022"** from the Start Menu for C++ work. Save yourself future pain: pin that shortcut to the taskbar.

### macOS

1. **Install Xcode Command Line Tools.** Open Terminal and run:
   ```bash
   xcode-select --install
   ```
   Click through the installer. This gives you Clang, Git, and make.
2. **Install Homebrew** if you don't have it:
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```
3. **Install the rest** via Homebrew:
   ```bash
   brew install cmake ninja node python@3.12 pkg-config
   ```

**Verify:**
```bash
clang --version
cmake --version
git --version
node --version
python3 --version
```

### Linux (Ubuntu 22.04+ or Debian 12+)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git curl zip unzip tar \
    pkg-config python3 python3-pip
# Node.js LTS
curl -fsSL https://deb.nodesource.com/setup_lts.x | sudo -E bash -
sudo apt install -y nodejs
# X11/OpenGL dev headers that Polyscope needs
sudo apt install -y libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxi-dev libgl1-mesa-dev libxxf86vm-dev libxkbcommon-dev
```

**Verify:**
```bash
g++ --version
cmake --version
git --version
node --version
python3 --version
```

---

## Step 2 — Install vcpkg (dependency manager)

vcpkg will auto-fetch GLM, Polyscope, ImGui, ImPlot, Assimp, doctest, nlohmann-json, fmt, and spdlog when CMake configures the project. Install vcpkg **once**, not per project.

Pick a location — I recommend `C:\dev\vcpkg` on Windows, `~/dev/vcpkg` on macOS/Linux.

### Windows (PowerShell)
```powershell
mkdir C:\dev
cd C:\dev
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
```
Close and reopen PowerShell so `VCPKG_ROOT` is set for future sessions. Verify with `echo $env:VCPKG_ROOT`.

### macOS / Linux
```bash
mkdir -p ~/dev
cd ~/dev
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
echo 'export VCPKG_ROOT="$HOME/dev/vcpkg"' >> ~/.zshrc   # or ~/.bashrc on Linux
echo 'export PATH="$VCPKG_ROOT:$PATH"' >> ~/.zshrc
source ~/.zshrc
```
Verify with `echo $VCPKG_ROOT`.

**Note:** you do **not** need to run `vcpkg install` manually. Because the project plan uses **manifest mode** (a `vcpkg.json` file in the project root), CMake will install the exact dependencies listed there automatically on first configure. That means the project is reproducible for every teammate without them touching vcpkg after the one-time bootstrap above.

---

## Step 3 — Install VS Code

Download and install from <https://code.visualstudio.com/download>.

On Windows, during install, check **all four optional tickboxes** ("Add 'Open with Code'..." and "Add to PATH"). Saves pain later.

Launch VS Code at least once to create its config directories before moving on.

---

## Step 4 — Install VS Code extensions

Open VS Code. Press `Ctrl+Shift+X` (Windows/Linux) or `Cmd+Shift+X` (macOS) to open the Extensions panel. Install these one by one, searching each name exactly:

| Extension | Publisher | What it does |
|---|---|---|
| **Claude Code** | Anthropic | The AI coding agent. Bundles the CLI. |
| **C/C++** | Microsoft | C++ IntelliSense and debugging. |
| **CMake Tools** | Microsoft | Build, configure, and run CMake projects from VS Code. |
| **clangd** | LLVM | Faster, more accurate C++ IntelliSense. Replaces MS's with something better. |
| **CodeLLDB** | Vadim Chugunov | Better debugger for Linux and macOS (Windows can also use MSVC's). |
| **clang-format** | xaver | Auto-format C++ on save per `.clang-format`. |
| **GitLens** | GitKraken | Git history in-line, extremely useful during code review. |
| **Even Better TOML** | tamasfe | Nicer TOML editing (used by clangd config). |

**Important conflict fix:** after installing both **C/C++** and **clangd**, open VS Code Settings (`Ctrl+,`), search for `C_Cpp.intelliSenseEngine`, and set it to **"disabled"**. This hands IntelliSense to clangd exclusively; otherwise both engines run and slow the editor to a crawl.

### Configure the Claude Code extension

1. Click the **Spark icon** in the VS Code Activity Bar (left sidebar) — it appeared after installing the Claude Code extension.
2. Click **"Sign in to Claude"**. A browser window opens. Log in with the Claude account that has your subscription (Pro, Max, Team, or Enterprise). Authorize the extension.
3. When you return to VS Code, you should see the Claude Code chat panel. Try a trivial prompt like "hello" to confirm it's working.

---

## Step 5 — Create the project folder

Make a home for the project. I'll assume `C:\dev\solar-cooker-rt` on Windows or `~/dev/solar-cooker-rt` on macOS/Linux.

### Windows
```powershell
cd C:\dev
mkdir solar-cooker-rt
cd solar-cooker-rt
git init
```

### macOS / Linux
```bash
cd ~/dev
mkdir solar-cooker-rt
cd solar-cooker-rt
git init
```

Now open this folder in VS Code: `File → Open Folder…` → pick `solar-cooker-rt`.

When VS Code asks "Do you trust the authors of the files in this folder?" click **Yes** — it's your own folder.

---

## Step 6 — Drop the plan document into the project

1. Copy the file `SolarCookerRayTracer_Plan.md` (from the previous step in our chat) into the root of the `solar-cooker-rt` folder.
2. Also create a very short `CLAUDE.md` file in the same folder. This is a special file Claude Code reads every session as persistent project context. Paste this into it:

```markdown
# Project: Solar Cooker Ray Tracer (C++ / CMake / vcpkg)

## Source of truth
Follow `SolarCookerRayTracer_Plan.md` exactly. That document specifies:
- Repository layout (Section 1)
- Dependencies and vcpkg manifest (Section 2)
- Build configuration (Section 3)
- Coding conventions (Section 4) — obey these strictly
- Module specifications (Section 5)
- Validation tests T1-T12 (Section 9)
- Phased build order (Section 10)

## Rules
- Execute phases in order. Do not skip ahead.
- After each phase, run the acceptance tests before proposing the next phase.
- All geometry and physics in `double` precision.
- Use `glm::dvec3` (not custom vector types).
- SI units throughout.
- No `shared_ptr` unless truly required.
- Every public class/function gets a one-line Doxygen comment.
- If you're unsure about a design decision, ask me before coding it.

## Current phase
Phase 1 — Bootstrap. Not started.

## Working style
- Propose a plan before making multi-file changes.
- Show diffs before writing large new files.
- Run `cmake --build` and `ctest` after meaningful changes and report results.
- Commit in logical units with descriptive messages.
```

This `CLAUDE.md` keeps Claude Code anchored to the plan document across sessions without you retyping context.

---

## Step 7 — Tell VS Code where vcpkg lives

Inside `solar-cooker-rt`, create a folder called `.vscode` and, inside it, a file called `settings.json`. Paste:

### Windows `.vscode/settings.json`
```json
{
  "cmake.configureSettings": {
    "CMAKE_TOOLCHAIN_FILE": "C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"
  },
  "cmake.generator": "Ninja",
  "C_Cpp.intelliSenseEngine": "disabled",
  "editor.formatOnSave": true,
  "editor.defaultFormatter": "xaver.clang-format",
  "files.associations": { "*.hpp": "cpp", "*.cpp": "cpp" }
}
```

### macOS / Linux `.vscode/settings.json`
```json
{
  "cmake.configureSettings": {
    "CMAKE_TOOLCHAIN_FILE": "${env:HOME}/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"
  },
  "cmake.generator": "Ninja",
  "C_Cpp.intelliSenseEngine": "disabled",
  "editor.formatOnSave": true,
  "editor.defaultFormatter": "xaver.clang-format",
  "files.associations": { "*.hpp": "cpp", "*.cpp": "cpp" }
}
```

Save the file. These settings make CMake Tools automatically pick up vcpkg-installed libraries and format code on save.

---

## Step 8 — Add a `.gitignore`

Create `.gitignore` in the project root with this content — otherwise you'll commit gigabytes of build artifacts:

```
# Build
build/
out/
cmake-build-*/

# vcpkg
vcpkg_installed/

# IDE
.vscode/*.log
.vs/
.idea/
*.user

# OS
.DS_Store
Thumbs.db

# Compiled
*.o
*.obj
*.exe
*.dll
*.so
*.dylib
*.a
*.lib
*.pdb
```

---

## Step 9 — Hand the project to Claude Code

Now the fun part. Open the Claude Code panel (Spark icon in the Activity Bar). In the chat box, paste this opening prompt:

> Read `SolarCookerRayTracer_Plan.md` and `CLAUDE.md` in this workspace. Then execute Phase 1 ("Bootstrap") from Section 10 of the plan. Specifically:
>
> 1. Create the directory structure exactly as described in Section 1.
> 2. Create the top-level `CMakeLists.txt` per Section 3.1 — static library `scrt_core`, test executable `scrt_tests` with doctest, CTest enabled, warnings-as-errors off in local dev.
> 3. Create `vcpkg.json` exactly as in Section 2.
> 4. Create `.clang-format` using LLVM style, 100-column width, 4-space indent.
> 5. Implement the Phase 1 files: `math/Vec.hpp`, `math/Constants.hpp`, `math/Rng.hpp`, `core/Ray.hpp`, `core/Hit.hpp`, `core/AABB.hpp`, `core/Transform.hpp`, `optics/Reflect.hpp`, `optics/Refract.hpp`, `optics/Fresnel.hpp`.
> 6. Write unit tests T1, T2, T3, T4, and T7 from Section 9. Make them pass.
>
> Before writing anything, show me the planned file list and wait for my approval. After approval, proceed. After each file is written, stop and summarize. After all files are written, configure CMake and run the tests with `ctest --output-on-failure`. Report the result.

Claude Code will propose a plan. Review it, click **Accept** (or reply with edits). It will then create files and show diffs inline — accept each one. When it runs terminal commands (`cmake`, `ctest`), it will ask permission the first time; grant it.

**Tip — use Plan Mode for big changes.** In the Claude Code extension, there's a toggle for "Plan Mode". Turn it on before handing over each new phase. Claude Code will first write a step-by-step plan in the chat, you approve it, and only then does it start editing. For a multi-file phase like Phase 2, this is vastly safer than auto-edit mode.

---

## Step 10 — Build and run the first time

After Claude Code reports Phase 1 is green:

1. Open the VS Code Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`).
2. Run **"CMake: Configure"**. Pick a kit when prompted (on Windows choose the MSVC 2022 kit; on macOS/Linux choose the default Clang/GCC kit). This runs vcpkg's dependency installation — the **first configure takes 10–30 minutes** because GLM, Polyscope, ImGui, ImPlot, Assimp, etc. all compile from source. Subsequent configures are instant.
3. Run **"CMake: Build"** (keyboard: `F7`). This compiles `scrt_core` and `scrt_tests`.
4. Run **"CMake: Run Tests"** (or hit the test beaker icon in the Activity Bar). You should see T1–T4 and T7 pass.

If the first configure fails with a vcpkg error, the most common cause is that `VCPKG_ROOT` isn't set in the VS Code environment. Close VS Code, reopen it **from a terminal where `echo $VCPKG_ROOT` works**, and re-run Configure.

---

## Step 11 — Commit your bootstrap

```bash
git add .
git commit -m "Phase 1: Bootstrap (math, core, optics primitives with doctest)"
```

Then push to GitHub (create an empty repo there first, then `git remote add origin …` and `git push -u origin main`). This is your safety net. Also set up GitHub Actions CI using the workflow stub described in the plan (Section 3.2) — Claude Code can generate it for you when you ask.

---

## Step 12 — Iterate on the remaining phases

For each subsequent phase, open a fresh Claude Code conversation (so its context isn't cluttered with the previous phase's details) and use a prompt like:

> Phase 2 of `SolarCookerRayTracer_Plan.md`. Update `CLAUDE.md`'s "Current phase" marker, then implement analytic surfaces (`Surface`, `Plane`, `Sphere`, `Paraboloid`), `PerfectMirror`, `Absorber`, `Pillbox` sun, `Aperture`, `Receiver`, `Scene` with linear-scan intersection, `FluxAccumulator`, and `Tracer` (single-threaded is fine). Implement tests T5, T6, T10. Use Plan Mode. Before coding, show me the planned file list for my approval.

Repeat for phases 3 through 7.

---

## Troubleshooting

**"Claude Code extension won't sign in"** — clear the extension's data (`Ctrl+Shift+P` → "Claude Code: Sign Out"), restart VS Code, sign in again. If your browser didn't open, copy the URL from the status bar notification manually.

**"CMake can't find `vcpkg.cmake`"** — `VCPKG_ROOT` isn't set in the shell VS Code inherited. On Windows, check via `echo $env:VCPKG_ROOT`. On macOS/Linux, `echo $VCPKG_ROOT`. Fix by setting it permanently (Windows: System Properties → Environment Variables; macOS/Linux: `~/.zshrc` or `~/.bashrc`).

**"First vcpkg configure is stuck at 'detecting compiler hash'"** — be patient. On Windows without a warm compile cache this step can take 5 minutes on the very first run.

**"Polyscope build fails with GLFW / OpenGL errors on Linux"** — you're missing the X11/GL dev headers; re-run the `apt install` line from Step 1 and retry.

**"clangd complains about missing headers"** — after CMake's first successful configure, a `compile_commands.json` file appears in `build/`. Create a symlink in the project root so clangd sees it:
- Linux/macOS: `ln -s build/compile_commands.json .`
- Windows (admin PowerShell): `New-Item -ItemType SymbolicLink -Path compile_commands.json -Target build\compile_commands.json`

**"Claude Code keeps making changes I didn't review"** — toggle **Plan Mode ON** in the extension toolbar. It will always propose a plan first and wait for approval.

**"I want to undo Claude's last change"** — press `Esc Esc` in the Claude Code chat, or type `/rewind`. This restores the checkpoint from before the last edit.

---

## Daily workflow (after setup)

1. Open VS Code in the project folder.
2. Open the Claude Code panel (Spark icon).
3. Tell it which part of the plan you're working on — e.g., *"Continue Phase 3. Implement `Dielectric` with split-mode Fresnel."*
4. Review diffs, accept or edit, let it run tests.
5. When green, commit.
6. Repeat.

Keep `CLAUDE.md` updated as you progress (move the "Current phase" marker). Claude Code will re-read it on every new session and stay oriented without you retyping the context.

---

## One nice-to-have: CMake presets

After Phase 1 is stable, ask Claude Code to add a `CMakePresets.json` at the project root with `debug` and `release` presets that bake in the vcpkg toolchain path. After that, teammates can just run `cmake --preset debug` once and never think about toolchain flags again. This makes the group onboarding a literal one-line command: clone the repo, run `cmake --preset debug && cmake --build build`, done.
