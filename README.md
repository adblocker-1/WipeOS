# WipeOS

**Securely erase hard drives and SSDs – with customer, technician and erasure report.**

WipeOS is a bootable USB stick for securely erasing hard drives and SSDs. It is based on
[ShredOS](https://github.com/PartialVolume/shredos.x86_64) and
[nwipe](https://github.com/martijnvanbrummelen/nwipe) and replaces their text interface with
a graphical, five-step workflow. For every device it creates a PDF erasure report with the
customer, the technician, the device serial number and every erased drive – saved directly
to the stick.

![Create job](docs/images/03-job.png)

---

## Contents

- [Features](#features)
- [Download and create the USB stick](#download-and-create-the-usb-stick)
- [User guide](#user-guide)
- [Erasure methods](#erasure-methods)
- [The erasure report](#the-erasure-report)
- [Files on the stick](#files-on-the-stick)
- [Boot options](#boot-options)
- [Important notes](#important-notes)
- [Building from source](#building-from-source)
- [Repository layout](#repository-layout)
- [Licenses and credits](#licenses-and-credits)

---

## Features

- **Create a job:** add a new customer or pick one from the customer list, choose the technician
- **Automatic device detection:** serial number, manufacturer and model are read from the BIOS (DMI)
- **System name, asset tag and note** for every device
- **Drive selection:** cards with model, serial number and capacity. The WipeOS stick itself is locked
- **All 12 nwipe erasure methods** and **all 8 random number generators**, with verification,
  rounds and an optional final blanking pass
- **Live progress** per drive with speed, remaining time and error count
- **PDF erasure report** per device, saved automatically on the stick
- **Mouse, touchscreen or keyboard** – US and German keyboard layouts, switchable in the UI
- **Consecutive job and customer numbers** that survive reboots
- The **classic nwipe interface** is still available (e.g. for ATA Secure Erase / NVMe Sanitize)

---

## Download and create the USB stick

1. **Download the image:** `WipeOS.img` from [Releases](../../releases/latest).
2. **Write it to a USB stick** (at least 128 MB). **Everything on the stick will be erased.**

| System | Tool | How |
|---|---|---|
| Windows | [Rufus](https://rufus.ie) | Select the stick → "SELECT" → `WipeOS.img` → Start. When asked, choose **"Write in DD Image mode"**. |
| Windows / macOS / Linux | [balenaEtcher](https://etcher.balena.io) | "Flash from file" → `WipeOS.img` → select the stick → "Flash!" |
| Linux | `dd` | `sudo dd if=WipeOS.img of=/dev/sdX bs=4M status=progress conv=fsync` (`sdX` = the stick!) |

3. **Boot from the stick:** plug it in, switch the computer on and open the boot menu
   (depending on the manufacturer **F12**, **F9**, **F11** or **Esc**). Choose the USB stick.
   "WipeOS" starts automatically after a few seconds.

> **Tip:** if the stick does not boot, disable "Secure Boot" in the BIOS/UEFI settings.

---

## User guide

### Step 1 – Create job

The interface appears right after booting. Serial number, manufacturer and model of the
device are already filled in.

![Start screen](docs/images/01-start.png)

- **Customer:** under **"New customer"** enter the company or name (required) and optionally
  contact person, address, phone/email and a reference. The customer automatically gets a
  customer number (e.g. `C-1004`) and is added to the customer list.
- **Existing customers** are listed under **"Existing"** and can be searched by name, city or
  customer number.

![Customer list](docs/images/02-customers.png)

- **Technician:** tap to select. Add new technicians with **"+ Add technician"** (name and ID).
- **Device:** enter a **system name** (required, e.g. `Accounting-NB-07`) and optionally an
  asset tag and a note.
- **Your company** (top right): your company name and address, shown on the report under
  "Performed by". You only need to enter this once.
- **Keyboard** (bottom left): switches between the US and German layout.

![Job filled in](docs/images/03-job.png)

**"Continue to drives"** creates the job and assigns a job number (e.g. `WO-2026-0001`).

### Step 2 – Select drives

All internal and connected drives are shown as cards. Tap the drives you want to erase.
The WipeOS stick is marked as **boot drive** and cannot be selected.

![Select drives](docs/images/04-drives.png)

### Step 3 – Choose method

Choose one of the 12 nwipe methods (see [Erasure methods](#erasure-methods)) and set the options
on the right: random generator, verification, rounds and an optional final blanking pass with
zeros. The plan shows the estimated time per drive.

![Choose method](docs/images/05-method.png)

**"Start erasure"** opens a confirmation dialog listing all affected drives. The erasure only
starts after **"Erase permanently"**.

![Confirmation](docs/images/06-confirm.png)

### Step 4 – Erasing

All drives are erased at the same time. The ring shows the overall progress, below it you see
speed, remaining time and errors per drive. Orange means "writing", blue means "verifying",
green means "done".

![Erasing](docs/images/07-erasing.png)

- **"Dim screen"** turns the display black. Tap to wake it up.
- **"Cancel"** asks for confirmation and then aborts. The abort is recorded in the report.
- **Do not switch off the device** while erasing.

### Step 5 – Finished

The result for each drive is shown and the erasure report has already been saved to the stick.

![Finished](docs/images/08-finished.png)

- **Next job:** a new device for the same customer and technician
- **Shut down / Restart:** afterwards the stick can be removed.

---

## Erasure methods

| Method | Passes | Description |
|---|---|---|
| **PRNG Stream** (recommended) | 1 | Fills the device with a stream from the random number generator. |
| Fill With Zeros | 1 | Fills the device with zeros (0x00). |
| Fill With Ones | 1 | Fills the device with ones (0xFF). |
| RCMP TSSIT OPS-II | 8 | Royal Canadian Mounted Police Technical Security Standard, OPS-II. |
| DoD 5220.22-M (Short) | 3 | U.S. Department of Defense 5220.22-M short 3-pass wipe (passes 1, 2 & 7). |
| DoD 5220.22-M (Full) | 7 | U.S. Department of Defense 5220.22-M full 7-pass wipe. |
| Gutmann Wipe | 35 | Peter Gutmann's method (Secure Deletion of Data from Magnetic and Solid-State Memory). |
| HMG IS5 Enhanced | 3 | UK HMG IS5 (Enhanced) sanitisation method for protectively marked or sensitive information. |
| Schneier Wipe | 7 | Bruce Schneier's 7-pass mixed-pattern algorithm. |
| BMB21-2019 | 6 | Chinese State Secrets Bureau BMB21-2019 technical requirement for data sanitisation. |
| Verify Zeros | read only | Only reads the device and checks that it is filled with zeros (0x00). |
| Verify Ones | read only | Only reads the device and checks that it is filled with ones (0xFF). |

**Options**

| Option | Values | Default |
|---|---|---|
| Random generator | ChaCha20 (CSPRNG), AES-256-CTR (CSPRNG, needs AES-NI), ISAAC-64 (CSPRNG), ISAAC (CSPRNG), XORoshiro-256, SplitMix64, Lagged Fibonacci, Mersenne Twister | ChaCha20 |
| Verification | Off, Last pass, All passes | Last pass |
| Rounds | 1–5 | 1 |
| Final blanking pass | writes zeros at the end, so the drive looks empty | off |

The verify methods only read the drive; their report is titled "Verification report".
For RCMP TSSIT OPS-II nwipe never adds a blanking pass.

---

## The erasure report

For every device **one PDF page** is saved in the `reports` folder on the stick, e.g.
`WO-2026-0001_Accounting-NB-07_PF3XK2LM.pdf`. It contains:

- Result (green: everything erased / red: errors or aborted)
- **Device:** system name, serial number, manufacturer, model, device type, asset tag, BIOS, UUID
- **Erased drives:** model, serial number, type, capacity, duration, result
- **Erasure:** start, finish, duration, method, passes and verification, random generator, HPA/DCO, errors, software
- **Customer & technician:** customer, customer no., address, contact, technician, your company
- Signature lines for the technician and the customer

<img src="docs/images/09-report.png" alt="Erasure report" width="600">

The complete nwipe log (`…_nwipe.log`) is saved next to the PDF.

---

## Files on the stick

The stick contains a normal FAT32 partition ("WIPEOS") that every PC can read:

| Path | Contents |
|---|---|
| `reports\` | Erasure reports (PDF) and nwipe logs |
| `wipeos\customers.csv` | Customer list, semicolon separated (Excel/LibreOffice) |
| `wipeos\technicians.csv` | Technicians (`ID;Name`) |
| `wipeos\company.txt` | Your company: line 1 name, line 2 address |
| `wipeos\job_counter.txt` | Counter for job numbers (year and number) |
| `GUIDE.txt` | Short guide |

Customers and technicians can be prepared on a PC or imported from another customer list.
The partition has about 15 MB free, enough for several hundred reports. Copy reports to your
PC regularly.

---

## Boot options

| Boot menu entry | Effect |
|---|---|
| **WipeOS** | Normal start with the new interface |
| **WipeOS (classic nwipe interface)** | Starts the original nwipe text interface |
| **WipeOS (nomodeset - for display problems)** | For devices whose graphics do not start correctly |

Additional parameters (press **e** in the boot menu and append them to the `linux` line):

- `wipeos_kbd=de` – German keyboard layout as default (can also be switched in the UI)
- `wipeos_gui=0` – classic interface

The classic interface is also available via **"Classic nwipe"** in step 1 (bottom left).
**Ctrl+Alt+F2** opens a console.

---

## Important notes

- **Test before real use:** WipeOS has been fully tested in a virtual machine
  (QEMU with SATA and NVMe drives), but not yet on many real devices. Do your first run with a
  drive whose data does not matter.
- **Hardware erase (ATA Secure Erase / NVMe Sanitize)** is not yet offered in the new interface.
  It is available in the classic nwipe interface.
- **AES-256-CTR** requires a CPU with AES-NI. If unsure, use ChaCha20 (default).
- **Time on the report** comes from the hardware clock of the erased device.
- **Without the WipeOS stick partition** (e.g. booted from the ISO) data is only kept in memory.
  The interface shows a warning in that case.

---

## Building from source

Requirements (Ubuntu 24.04 or Debian 12):

```bash
sudo apt install build-essential git rsync bc cpio unzip file wget \
  libncurses-dev libssl-dev libelf-dev mtools dosfstools pkg-config automake
```

Build:

```bash
git clone --recurse-submodules https://github.com/adblocker-1/WipeOS.git
cd WipeOS
./build.sh
```

`build.sh` fetches the tested ShredOS version, applies the WipeOS changes and builds everything
with Buildroot. The finished image is written to `out/WipeOS.img`. The first build takes
2–4 hours and needs about 30 GB of disk space; later builds are much faster.

Try it without real hardware (requires `qemu-system-x86`):

```bash
tools/vm-test.sh out/WipeOS.img
```

The interface can also be built and tested on a normal Linux PC without a screen; it then saves
screenshots:

```bash
cd gui && make
SHREDOS_HEADLESS=1280x800 SHREDOS_TEST_SCRIPT="demo;shot:/tmp/step1.ppm" ./shredos-gui
```

---

## Repository layout

```
gui/                      Graphical interface (C, LVGL 9.2, framebuffer + evdev)
  app/ui.c                Screens of the five steps
  app/report.c            PDF erasure report (PDFGen)
  app/wipe.c              Methods, random generators, runs nwipe and reads progress
  app/sysinfo.c           Serial number (DMI) and drives (/sys/block)
  app/storage.c           Customers, technicians and numbers on the stick
  app/input.c             Mouse, touch and keyboard (US and German layouts)
  lvgl/                   LVGL (git submodule)
buildroot/                Additions to the ShredOS build
  shredos.patch           Changes to ShredOS (startup, boot menu, name, packages)
  package/shredos-gui/    Buildroot package of the interface
  package/nwipe/          nwipe patch: --status-file option for live progress
  board/shredos/          Startup script, short guide, company.txt template
docs/images/              Screenshots used in this guide
tools/                    QEMU test script and QMP helper
build.sh                  Builds the complete image
```

How it works: at boot `shredos_gui_launcher` mounts the stick partition and starts the
interface. It draws directly to the framebuffer and, to erase, runs
`nwipe --nogui --autonuke` with the selected method, options and drives. Through the new
`--status-file` option nwipe writes its progress to a file every second, which the interface
displays. At the end the interface creates the PDF report from it.

---

## Licenses and credits

- **WipeOS interface** (`gui/app`, scripts): GNU GPL v3, see [LICENSE](LICENSE)
- **nwipe patch** (`buildroot/package/nwipe`): GNU GPL v2, like nwipe
- **[ShredOS](https://github.com/PartialVolume/shredos.x86_64)** by PartialVolume: GNU GPL v2
- **[nwipe](https://github.com/martijnvanbrummelen/nwipe)** by Martijn van Brummelen and PartialVolume: GNU GPL v2
- **[LVGL](https://lvgl.io)**: MIT
- **PDFGen** by Andre Renaud: public domain
- **IBM Plex** fonts: SIL Open Font License 1.1 (`gui/app/IBM-Plex-OFL-1.1.txt`)

Many thanks to the developers of ShredOS and nwipe – without their work WipeOS would not exist.
