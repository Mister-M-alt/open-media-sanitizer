// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "workspace.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean fail(GError **error, const char *message)
{
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, message);
    return FALSE;
}

gboolean workspace_init(Workspace *w, const char *binary, gboolean demo, GError **error)
{
    *w = (Workspace){.binary = g_strdup(binary), .demo = demo};
    if (!demo) return TRUE;
    w->demo_dir = g_dir_make_tmp("oms-demo-XXXXXX", error);
    if (!w->demo_dir) return FALSE;
    for (unsigned i = 0; i < 2; ++i) {
        char *name = g_strdup_printf("sample-%u.img", i + 1);
        w->demo_paths[i] = g_build_filename(w->demo_dir, name, NULL);
        g_free(name);
        int fd = open(w->demo_paths[i], O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) return fail(error, g_strerror(errno));
        gboolean ok = write(fd, "Disposable demo data\n", 21) == 21 &&
                      ftruncate(fd, (off_t)(32U << i) * 1024 * 1024) == 0;
        if (close(fd) != 0) ok = FALSE;
        if (!ok) return fail(error, "Could not create demonstration media.");
    }
    return TRUE;
}

void workspace_clear(Workspace *w)
{
    for (unsigned i = 0; i < 2; ++i) {
        if (w->demo_paths[i]) g_unlink(w->demo_paths[i]);
        g_free(w->demo_paths[i]);
    }
    if (w->demo_dir) g_rmdir(w->demo_dir);
    g_free(w->demo_dir);
    g_free(w->binary);
    *w = (Workspace){0};
}

#ifndef OMS_QUERY_TIMEOUT_MS
#define OMS_QUERY_TIMEOUT_MS 30000
#endif

typedef struct {
    GMainLoop *loop;
    GSubprocess *process;
    GCancellable *cancel;
    char *output, *diagnostic;
    GError *error;
    gboolean ok, timed_out;
} Query;

static gboolean query_timeout(gpointer data)
{
    Query *query = data;
    query->timed_out = TRUE;
    g_subprocess_force_exit(query->process);
    g_cancellable_cancel(query->cancel);
    return G_SOURCE_REMOVE;
}

static void query_done(GObject *source, GAsyncResult *result, gpointer data)
{
    Query *query = data;
    query->ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result,
        &query->output, &query->diagnostic, &query->error);
    g_main_loop_quit(query->loop);
}

static JsonNode *query(const char *const *argv, GError **error)
{
    GSubprocess *process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                           G_SUBPROCESS_FLAGS_STDERR_PIPE, error);
    if (!process) return NULL;
    /* A private context avoids re-entering GTK callbacks while replacing the
     * inventory, while still allowing the deadline to interrupt stalled I/O. */
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    Query request = {.process = process, .loop = g_main_loop_new(context, FALSE), .cancel = g_cancellable_new()};
    GSource *deadline = g_timeout_source_new(OMS_QUERY_TIMEOUT_MS);
    g_source_set_callback(deadline, query_timeout, &request, NULL);
    g_source_attach(deadline, context);
    g_subprocess_communicate_utf8_async(process, NULL, request.cancel, query_done, &request);
    g_main_loop_run(request.loop);
    g_source_destroy(deadline); g_source_unref(deadline);
    g_main_context_pop_thread_default(context);
    g_main_loop_unref(request.loop); g_main_context_unref(context);
    g_object_unref(request.cancel);
    char *output = request.output, *diagnostic = request.diagnostic;
    gboolean ok = request.ok;
    if (request.timed_out) {
        g_clear_error(&request.error);
        fail(error, "Device inspection timed out. Check the device connection and retry.");
        ok = FALSE;
    } else if (request.error) g_propagate_error(error, request.error);
    if (ok && !g_subprocess_get_successful(process)) {
        fail(error, diagnostic && *diagnostic ? diagnostic : "Device inspection failed.");
        ok = FALSE;
    }
    JsonParser *parser = json_parser_new();
    JsonNode *node = NULL;
    if (ok && json_parser_load_from_data(parser, output, -1, error)) {
        JsonNode *root = json_parser_get_root(parser);
        if (root) node = json_node_copy(root);
        else fail(error, "The backend returned an empty device inventory.");
    }
    g_object_unref(parser);
    g_object_unref(process);
    g_free(output);
    g_free(diagnostic);
    return node;
}

JsonArray *workspace_inventory(Workspace *w, GError **error)
{
    JsonArray *array = json_array_new();
    if (!w->demo) {
        const char *argv[] = {w->binary, "list", "--json", NULL};
        JsonNode *node = query(argv, error);
        json_array_unref(array);
        if (!node) return NULL;
        if (!JSON_NODE_HOLDS_ARRAY(node)) {
            json_node_free(node);
            fail(error, "Invalid device inventory.");
            return NULL;
        }
        array = json_array_ref(json_node_get_array(node));
        json_node_free(node);
        return array;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const char *argv[] = {w->binary, "inspect", w->demo_paths[i], "--json", "--allow-file", NULL};
        JsonNode *node = query(argv, error);
        if (!node || !JSON_NODE_HOLDS_OBJECT(node)) {
            if (node) { json_node_free(node); fail(error, "Invalid demo inventory."); }
            json_array_unref(array);
            return NULL;
        }
        char *model = g_strdup_printf("Disposable medium %u", i + 1);
        json_object_set_string_member(json_node_get_object(node), "model", model);
        g_free(model);
        json_array_add_element(array, node);
    }
    return array;
}

const char *target_blocked(JsonObject *target)
{
    const char *keys[] = {"read_only", "mounted", "swap_active", "has_holders"};
    const char *reasons[] = {"Read-only", "Mounted or mount status unavailable",
                            "Swap active or status unavailable", "In use or status unavailable"};
    for (unsigned i = 0; i < G_N_ELEMENTS(keys); ++i)
        if (!json_object_has_member(target, keys[i]) || json_object_get_boolean_member(target, keys[i]))
            return reasons[i];
    if (json_object_get_int_member(target, "size_bytes") <= 0) return "Empty device";
    return NULL;
}

GSubprocess *workspace_start(Workspace *w, JsonObject *target, Settings settings,
                            const char *confirmation, GError **error)
{
    const char *path = json_object_get_string_member(target, "path");
    const char *kind = json_object_get_string_member(target, "kind");
    const char *identity = json_object_get_string_member(target, "identity");
    const char *reason = target_blocked(target);
    if (reason) { fail(error, reason); return NULL; }
    if (!path || g_strcmp0(path, confirmation) != 0) {
        fail(error, "Type the complete target path exactly as displayed."); return NULL;
    }
    if (!identity || !*identity || (w->demo ?
        (g_strcmp0(kind, "file") || (g_strcmp0(path, w->demo_paths[0]) && g_strcmp0(path, w->demo_paths[1]))) :
        g_strcmp0(kind, "block"))) {
        fail(error, "This target is not allowed in the current application mode."); return NULL;
    }
    if (!settings.method || (strcmp(settings.method, "zero") && strcmp(settings.method, "ones") &&
        strcmp(settings.method, "random")) || settings.passes < 1 || settings.passes > 16 ||
        (settings.verify && !strcmp(settings.method, "random"))) {
        fail(error, "Invalid write pattern, pass count, or verification setting."); return NULL;
    }
    char passes[16];
    g_snprintf(passes, sizeof(passes), "%u", settings.passes);
    const char *argv[20] = {w->binary, "erase", path, "--method", settings.method,
        "--passes", passes, "--execute", "--confirm", confirmation, "--expect-id", identity, "--progress"};
    unsigned count = 13;
    if (settings.verify) argv[count++] = "--verify";
    if (w->demo) argv[count++] = "--allow-file";
    argv[count] = NULL;
    return g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                            G_SUBPROCESS_FLAGS_STDERR_MERGE, error);
}

char *utc_now(void)
{
    GDateTime *now = g_date_time_new_now_utc();
    char *text = g_date_time_format_iso8601(now);
    g_date_time_unref(now);
    return text;
}

JsonObject *report_new(JsonObject *target, Settings settings, gboolean demo, const char *started,
                       gint64 clock, const char *outcome, int code, const char *log)
{
    JsonObject *record = json_object_new();
    char *id = g_uuid_string_random(), *finished = utc_now();
    json_object_set_int_member(record, "schema_version", 1);
    json_object_set_string_member(record, "id", id);
    json_object_set_string_member(record, "application", "Open Media Sanitizer");
    json_object_set_string_member(record, "mode", demo ? "demo" : "live");
    json_object_set_string_member(record, "started_at", started);
    json_object_set_string_member(record, "finished_at", finished);
    json_object_set_double_member(record, "elapsed_seconds", (g_get_monotonic_time() - clock) / 1e6);
    json_object_set_object_member(record, "target", json_object_ref(target));
    json_object_set_string_member(record, "method", settings.method);
    json_object_set_int_member(record, "passes", settings.passes);
    json_object_set_boolean_member(record, "verification_requested", settings.verify);
    json_object_set_boolean_member(record, "verification_completed", settings.verify && !strcmp(outcome, "completed"));
    json_object_set_string_member(record, "outcome", outcome);
    json_object_set_int_member(record, "exit_code", code);
    JsonArray *lines = json_array_new();
    char **parts = g_strsplit(log, "\n", -1);
    for (unsigned i = 0; parts[i]; ++i) json_array_add_string_element(lines, parts[i]);
    json_object_set_array_member(record, "log", lines);
    json_object_set_string_member(record, "scope", "This record describes writes to the exposed logical address range. "
        "It does not certify sanitization of hidden, remapped, or overprovisioned storage.");
    g_strfreev(parts); g_free(id); g_free(finished);
    return record;
}

static void row(GString *html, const char *name, const char *value)
{
    char *escaped = g_markup_escape_text(value, -1);
    g_string_append_printf(html, "<tr><th>%s</th><td>%s</td></tr>", name, escaped);
    g_free(escaped);
}

char *report_render(JsonObject *record, gboolean html)
{
    if (!html) {
        JsonNode *node = json_node_new(JSON_NODE_OBJECT);
        json_node_set_object(node, record);
        char *text = json_to_string(node, TRUE);
        json_node_free(node);
        return text;
    }
    GString *page = g_string_new("<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>Media operation record</title><style>body{font:16px system-ui;background:#edf2f6;color:#142a38;padding:4vw}"
        "main{max-width:900px;margin:auto;background:white;padding:40px;border-radius:16px}"
        "table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:12px;border-bottom:1px solid #dce5eb;overflow-wrap:anywhere}"
        "pre{white-space:pre-wrap;overflow-wrap:anywhere}h2{color:#087f75}@media print{body{padding:0}main{padding:0}}"
        "</style><main><h2>OPEN MEDIA SANITIZER</h2><h1>Operation record</h1><table>");
    const char *keys[] = {"id", "mode", "outcome", "method", "started_at", "finished_at"};
    const char *names[] = {"Record", "Mode", "Outcome", "Pattern", "Started (UTC)", "Finished (UTC)"};
    for (unsigned i = 0; i < G_N_ELEMENTS(keys); ++i) row(page, names[i], json_object_get_string_member(record, keys[i]));
    JsonObject *target = json_object_get_object_member(record, "target");
    row(page, "Target", json_object_get_string_member(target, "path"));
    row(page, "Model", json_object_get_string_member(target, "model"));
    char *size = g_format_size_full(json_object_get_int_member(target, "size_bytes"), G_FORMAT_SIZE_IEC_UNITS);
    row(page, "Size", size); g_free(size);
    char *passes = g_strdup_printf("%" G_GINT64_FORMAT, json_object_get_int_member(record, "passes"));
    row(page, "Passes", passes); g_free(passes);
    char *elapsed = g_strdup_printf("%.3f", json_object_get_double_member(record, "elapsed_seconds"));
    row(page, "Duration (seconds)", elapsed); g_free(elapsed);
    row(page, "Read-back verified", json_object_get_boolean_member(record, "verification_completed") ? "Yes" : "No");
    g_string_append(page, "</table><p>");
    char *scope = g_markup_escape_text(json_object_get_string_member(record, "scope"), -1);
    g_string_append(page, scope); g_free(scope);
    g_string_append(page, "</p><h2>Operation log</h2><pre>");
    JsonArray *lines = json_object_get_array_member(record, "log");
    for (unsigned i = 0; i < json_array_get_length(lines); ++i) {
        char *line = g_markup_escape_text(json_array_get_string_element(lines, i), -1);
        g_string_append_printf(page, "%s\n", line); g_free(line);
    }
    g_string_append(page, "</pre></main></html>\n");
    return g_string_free(page, FALSE);
}

gboolean report_save(JsonObject *record, const char *path, gboolean html, GError **error)
{
    JsonObject *target = json_object_get_object_member(record, "target");
    const char *target_path = json_object_get_string_member(target, "path");
    struct stat dest, source;
    char *canonical = g_canonicalize_filename(path, NULL);
    gboolean same = !strcmp(canonical, target_path);
    g_free(canonical);
    if (lstat(path, &dest) == 0) {
        if (!S_ISREG(dest.st_mode)) return fail(error, "Reports must be saved to a regular file, not a link or device.");
        if (stat(target_path, &source) == 0 && source.st_dev == dest.st_dev && source.st_ino == dest.st_ino) same = TRUE;
    } else if (errno != ENOENT) return fail(error, g_strerror(errno));
    if (same) return fail(error, "Choose a report destination separate from the erase target.");
    char *directory = g_path_get_dirname(path);
    char *temporary = g_build_filename(directory, ".oms-report-XXXXXX", NULL);
    int fd = g_mkstemp_full(temporary, O_WRONLY | O_CLOEXEC, 0600);
    char *text = report_render(record, html);
    gboolean ok = fd >= 0;
    size_t offset = 0, length = strlen(text);
    while (ok && offset < length) {
        ssize_t count = write(fd, text + offset, length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) ok = FALSE; else offset += (size_t)count;
    }
    if (ok && fsync(fd) != 0) ok = FALSE;
    if (fd >= 0 && close(fd) != 0) ok = FALSE;
    if (ok && g_rename(temporary, path) != 0) ok = FALSE;
    if (!ok) fail(error, "Could not save the report. Check destination permissions and free space.");
    g_unlink(temporary);
    g_free(text); g_free(directory); g_free(temporary);
    return ok;
}
