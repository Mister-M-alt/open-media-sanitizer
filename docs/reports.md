# Reports and interpretation

[Documentation index](README.md) · [Operator guide](operator-guide.md)

This guide is for operators exporting records and people reviewing them later.
The desktop creates a record when an attempted operation finishes, fails to
start, fails during execution, or is cancelled. Simply viewing a plan does not
create an operation record.

## Export and retain a record

1. Wait for the operation's final outcome in Activity.
2. Open **Reports** and select the relevant row.
3. Choose **Export JSON** for structured data or **Export printable HTML** for
   a document that can be opened in a browser. You can export both.
4. Save to a regular file separate from the erase target. Use a distinct name
   for each record; the save dialog asks before replacing an existing file.
5. Confirm that the exported file is accessible before closing the session.

Exports reject a device, a symbolic-link destination, and the erase target.
Files are written through a temporary file and renamed into place, with `0600`
permissions: only the owner has read/write access. In the boot edition they are
normally owned by root, so arrange access when handing them to another person.

HTML contains escaped text and no scripts. Open it in a browser on a computer
with a browser installed, then use that browser's print or print-to-PDF feature.
The application does not directly generate PDF, and the boot image does not
include a browser or print service.

Report history is kept in memory. There is no database, automatic export,
cloud upload, or report-import feature. On boot media, export to
[separate mounted storage](boot-images.md#use) before shutdown; a file saved
under `/tmp` disappears with the session. Mounting the report drive also blocks
it from erasure.

## Read the outcome correctly

| Value | Meaning |
| --- | --- |
| `completed` | The backend exited successfully and the desktop received its output without a stream failure |
| `failed` | Startup, validation, writing, reading, flushing, closing, or supervision failed; inspect the log |
| `cancelled` | A stop was requested and the operation did not complete successfully |

`verification_requested` describes the settings. `verification_completed` is
true only when verification was requested and the whole operation completed.
A completed random write, or a completed write without verification, has
`verification_completed: false`. A cancelled operation may have already
overwritten much of the target. A stop request arriving after successful work
can still yield `completed`.

`mode: demo` means the target was a disposable demonstration file. It is never
evidence that a physical device was erased. Read both the mode and outcome.

## JSON schema version 1

The JSON export contains the following fields. Preserve unknown fields when
building an integration, and check `schema_version` before relying on a layout.

| Field | Type | Meaning |
| --- | --- | --- |
| `schema_version` | Integer | Currently `1` |
| `id` | String | UUID identifying this operation record |
| `application` | String | `Open Media Sanitizer`; not an application version |
| `mode` | String | `demo` or `live` |
| `started_at`, `finished_at` | String | ISO 8601 timestamps in UTC |
| `elapsed_seconds` | Number | Duration measured with a monotonic clock |
| `target` | Object | Inspection snapshot: path, model, size, flags, and identity; see [inventory fields](cli-reference.md#json-inventory) |
| `method` | String | `zero`, `ones`, or `random` |
| `passes` | Integer | Requested write-pass count |
| `verification_requested` | Boolean | Whether read-back was requested |
| `verification_completed` | Boolean | Whether requested verification and the operation completed |
| `outcome` | String | `completed`, `failed`, or `cancelled` |
| `exit_code` | Integer | Backend exit code, or `-1` when a normal exit code is unavailable |
| `log` | Array of strings | Captured diagnostic lines |
| `scope` | String | Description of the operation's limits |

Timestamps depend on the machine's system clock. An offline machine may have an
incorrect clock; record that fact if relevant. Duration is measured separately.
The target object records the selected inspection snapshot, not a continuously
updated inventory. The application version is not a separate field in schema 1;
retain the release tag or `oms --version` output with records when needed.

The desktop retains approximately the most recent 64 KiB of diagnostic text per
operation, dropping whole lines to keep within the limit. Progress event lines
drive the progress display and are not included in this diagnostic log. Do not
assume the export contains every message from a long-running operation. HTML
presents the main fields and log; JSON carries the full structured record.

## What a report establishes

The record describes the software's attempt to overwrite the exposed logical
range. Read-back checks bytes returned through the operating system and device
caches. Neither the record nor its verification field proves treatment of
hidden, remapped, reserved, or overprovisioned storage.

Records are not digitally signed, independently attested, or tamper-evident.
They are operation records, not sanitization certificates. If an organization
requires additional identity checks, retention controls, or another erasure
method, those processes must be supplied separately.

Before sharing a report, review paths, model details, and diagnostic text for
information you do not intend to disclose. Preserve an unmodified original
when producing a redacted copy for a support request.
