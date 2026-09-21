# Operator guide

[Documentation index](README.md) · [Troubleshooting](troubleshooting.md)

This guide is for someone using the desktop workspace. You do not need to write
C code. Begin with the demonstration before working with physical storage.

## Choose how to run

| Your situation | Start here |
| --- | --- |
| You have an existing Linux desktop | Install the [desktop dependencies](../README.md#run-on-an-existing-linux-desktop), then run `./start.sh --demo` from the checkout |
| You need to work without an installed OS | Download and prepare the [standalone boot image](../README.md#download-and-boot) |
| You have only a terminal | Use the [CLI reference](cli-reference.md), starting with its temporary-file example |

Downloading private releases requires repository access and an internet
connection. Running the prepared boot image requires neither an account nor a
network connection. Use a graphical display and keyboard; a mouse is optional.

For the desktop source route, a checkout means the downloaded project folder
containing `Makefile` and `start.sh`. You can use Git as shown in the
[developer guide](development.md#get-a-checkout), or use GitHub's **Code → Download
ZIP** and extract it. Open a terminal in that folder for the commands. The
prebuilt boot-image route does not require compiling the application.

The boot menu offers **Open Media Sanitizer**, **Disposable demonstration**, and
**Recovery console**. The first opens the live device workspace; starting it
does not begin an erase. The demo entry only offers its own temporary files.

## Try the demo first

1. Launch `./start.sh --demo`, or choose **Disposable demonstration** at boot.
2. Check that the header says **DEMO · disposable files**. Storage shows two
   sample files of 32 MiB and 64 MiB.
3. Select one sample. Choose **Zeroes**, one pass, and **Verify read-back**.
4. Select **Review operation**. Check the path, size, pattern, and pass count.
5. Type the complete displayed path in the confirmation field. It changes
   between demo sessions; do not reuse a path from a screenshot.
6. Select **Overwrite this medium**. **Activity** shows writing and then
   verification. Wait for the final outcome.
7. Open **Reports**, select the resulting row, and export JSON or printable HTML.

The demo runs the real backend against its two disposable files. Normal exit
removes those files. Report history is held only for the current application
session, so export records you want to retain before closing.

## Understand the workspace

| Page | Purpose |
| --- | --- |
| Storage | Refresh the inventory, select one medium, and choose settings |
| Review | Check the proposed operation and type the full target path |
| Activity | Follow the current phase, read diagnostic messages, or cancel |
| Reports | Select an operation from this session and export its record |
| About | Read the application version, license, and scope |

Use Tab and Shift+Tab to move between controls, arrow keys in lists and choices,
and Space/Enter to activate a focused control. The CLI is available when a
graphical session is unavailable. Screen-reader behavior has not been audited.

## Prepare for a real operation

Only proceed on storage you are authorized to overwrite. Make any needed backup
before selecting a target. Identify the intended device by its physical role,
model, and capacity as well as its current Linux path. Paths can change when
devices are disconnected or the machine restarts.

The live workspace header says **LIVE · local devices**. Real writes require
root access. The bootable edition already runs locally with this access. On an
existing desktop, build as your regular user first; the
[launch instructions](../README.md#run-on-an-existing-linux-desktop) explain
privileged use. The app does not elevate itself.

Mounted devices, active swap, read-only media, and devices used by another block
device are blocked. Missing usage information is also a reason to block. The
boot medium remains mounted and is therefore blocked. An **Available** status
means the current checks found no such block; it is not proof that you selected
the right device.

If storage is in use, identify its role before changing mounts, swap, or storage
mappings. Boot from separate media when the target contains the running OS.
There is no application option to bypass these checks.

## Select settings and execute

| Setting | Behavior |
| --- | --- |
| Zeroes | Writes `0x00`; supports read-back verification |
| Ones | Writes `0xff`; supports read-back verification |
| Random | Uses `/dev/urandom`; read-back verification is unavailable |
| Passes | 1–16 complete writes; more passes take more time and do not establish firmware sanitization |
| Verify read-back | Checks the final zero/ones pattern after all write passes |

The desktop initially selects zeroes, one pass, and verification. The CLI has
different verification defaults; see its reference before using it.

Select the medium and settings in **Storage**, then choose **Review operation**.
Check the details again and type the exact full path, including `/dev/`. The
start button becomes available only when the text matches. Changing settings
requires returning to Storage and reviewing the new operation.

Once started, the backend rechecks identity and usage before writing. The GUI
runs one operation at a time. Progress describes the current phase and pass;
it may restart at zero for another pass or for verification. Reaching 100% in a
phase is not the final outcome: wait for **completed**, **failed**, or **cancelled**.

## Cancel or handle a failure

Choose **Stop operation** in Activity to request a stop. The app waits for the
backend to stop and attempt to flush writes. Closing the window during an
operation offers **Keep running** or **Stop and close**. Cancellation may take
time when a device or driver is slow.

Cancellation and failure can leave the target partially overwritten. There is
no undo, rollback, or resume feature. Do not treat a failed or cancelled run as
a completed operation. Save the record and diagnostics before deciding whether
to start a new operation. If a cancel request races with successful completion,
the final outcome can still be **completed**.

## Save reports and shut down

Select each record in **Reports** and choose **Export JSON** or **Export printable
HTML**. The [report guide](reports.md) explains the fields and their limits.
The app does not automatically save or reload report history.

On boot media, writable session data is in RAM and disappears on shutdown.
Arrange separate report storage, then follow the
[recovery-console mounting instructions](boot-images.md#use). Return to the
desktop with Ctrl+Alt+F1 and export there. Exported HTML can be opened and printed
from another computer with a browser; the boot image does not include a browser.

Wait for any operation to finish, export the records, and unmount report storage
before unplugging it. From the boot edition's Ctrl+Alt+F2 recovery shell, use
`poweroff` to shut down. Closing its desktop window starts a new workspace;
it does not power off the computer.
