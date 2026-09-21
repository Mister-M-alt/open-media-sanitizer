// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "workspace.h"
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static Workspace workspace;
static JsonObject *target;
static const Settings settings = {"zero", 1, TRUE};
static char *binary, *fault_binary;

static void reset_file(void)
{
    char data[1024 * 1024 + 73];
    memset(data, 0xa5, sizeof(data));
    g_assert_true(g_file_set_contents(workspace.demo_paths[0], data, sizeof(data), NULL));
    JsonArray *targets = workspace_inventory(&workspace, NULL);
    g_assert_nonnull(targets);
    if (target) json_object_unref(target);
    target = json_object_ref(json_array_get_object_element(targets, 0));
    json_array_unref(targets);
}

static void check_pattern(unsigned char pattern)
{
    char *data; gsize length;
    g_assert_true(g_file_get_contents(workspace.demo_paths[0], &data, &length, NULL));
    g_assert_cmpuint(length, ==, 1024 * 1024 + 73);
    for (gsize i = 0; i < length; ++i) g_assert_cmpuint((unsigned char)data[i], ==, pattern);
    g_free(data);
}

static GSubprocess *start(Settings options, GError **error)
{
    return workspace_start(&workspace, target, options, workspace.demo_paths[0], error);
}

static char *collect(GSubprocess *process, gboolean success)
{
    char *output = NULL;
    g_assert_nonnull(process);
    g_assert_true(g_subprocess_communicate_utf8(process, NULL, NULL, &output, NULL, NULL));
    g_assert_cmpint(g_subprocess_get_successful(process), ==, success);
    g_object_unref(process);
    return output;
}

static void test_reports(void)
{
    reset_file();
    char *started = utc_now(); gint64 clock = g_get_monotonic_time();
    char *output = collect(start(settings, NULL), TRUE);
    check_pattern(0);
    g_assert_nonnull(strstr(output, "OMS_PROGRESS verifying"));
    json_object_set_string_member(target, "model", "<script>alert(1)</script>");
    JsonObject *record = report_new(target, settings, TRUE, started, clock, "completed", 0, "<img src=x>");
    char *html = report_render(record, TRUE);
    g_assert_null(strstr(html, "<script>")); g_assert_null(strstr(html, "<img "));
    g_assert_nonnull(strstr(html, "&lt;script&gt;"));
    char *path = g_build_filename(workspace.demo_dir, "operation.json", NULL);
    g_assert_true(report_save(record, path, FALSE, NULL));
    struct stat status;
    g_assert_cmpint(stat(path, &status), ==, 0);
    g_assert_cmpuint(status.st_mode & 0777, ==, 0600);
    JsonParser *parser = json_parser_new();
    g_assert_true(json_parser_load_from_file(parser, path, NULL));
    g_assert_true(json_object_get_boolean_member(json_node_get_object(json_parser_get_root(parser)), "verification_completed"));
    g_assert_false(report_save(record, workspace.demo_paths[0], FALSE, NULL));
    char *alias = g_build_filename(workspace.demo_dir, "alias", NULL);
    g_assert_cmpint(link(workspace.demo_paths[0], alias), ==, 0);
    g_assert_false(report_save(record, alias, TRUE, NULL)); g_unlink(alias);
    g_assert_cmpint(symlink(path, alias), ==, 0);
    g_assert_false(report_save(record, alias, FALSE, NULL)); g_unlink(alias);
    g_assert_true(report_save(record, path, TRUE, NULL));
    g_unlink(path); g_free(alias); g_free(path); g_free(html); g_free(started); g_free(output);
    g_object_unref(parser); json_object_unref(record);
}

static void test_validation(void)
{
    reset_file();
    g_assert_null(workspace_start(&workspace, target, settings, "incorrect", NULL));
    const char *flags[] = {"read_only", "mounted", "swap_active", "has_holders"};
    for (unsigned i = 0; i < G_N_ELEMENTS(flags); ++i) {
        json_object_set_boolean_member(target, flags[i], TRUE);
        g_assert_null(start(settings, NULL));
        json_object_set_boolean_member(target, flags[i], FALSE);
    }
    g_assert_null(start((Settings){"random", 1, TRUE}, NULL));
    g_assert_null(start((Settings){"zero", 0, TRUE}, NULL));
    g_assert_null(start((Settings){"zero", 17, TRUE}, NULL));
    g_assert_null(start((Settings){"invalid", 1, FALSE}, NULL));
    json_object_set_string_member(target, "path", "/tmp/not-owned.img");
    g_assert_null(workspace_start(&workspace, target, settings, "/tmp/not-owned.img", NULL));
    json_object_set_string_member(target, "path", workspace.demo_paths[0]);
    json_object_set_string_member(target, "kind", "block");
    g_assert_null(start(settings, NULL));
    json_object_set_string_member(target, "kind", "file");
    json_object_set_string_member(target, "identity", "outdated");
    char *output = collect(start(settings, NULL), FALSE);
    g_assert_nonnull(strstr(output, "identity changed"));
    check_pattern(0xa5); g_free(output);
}

static void test_file_guards(void)
{
    reset_file();
    char *alias = g_build_filename(workspace.demo_dir, "hardlink", NULL);
    g_assert_cmpint(link(workspace.demo_paths[0], alias), ==, 0);
    g_free(collect(start(settings, NULL), FALSE)); check_pattern(0xa5);
    g_unlink(alias); g_free(alias);
    int fd = open(workspace.demo_paths[0], O_RDWR);
    g_assert_cmpint(fd, >=, 0); g_assert_cmpint(flock(fd, LOCK_EX | LOCK_NB), ==, 0);
    g_free(collect(start(settings, NULL), FALSE)); check_pattern(0xa5);
    close(fd);
}

static void test_faults(void)
{
    const char *faults[] = {"short-write", "write-eintr", "write-zero", "write-error", "read-eof",
        "verify-mismatch", "flush-error", "close-error", "cancel", "changed-size"};
    const char *messages[] = {"Erase completed", "Erase completed", "no write progress", "write failed",
        "unexpected end of target", "verification mismatch", "cannot flush", "cannot close", "interrupted", "does not match"};
    for (unsigned i = 0; i < G_N_ELEMENTS(faults); ++i) {
        reset_file();
        /* Launcher-local environment: no process-global setenv after GLib threads start. */
        GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
        g_subprocess_launcher_setenv(launcher, "OMS_TEST_FAULT", faults[i], TRUE);
        GSubprocess *process = g_subprocess_launcher_spawn(launcher, NULL, fault_binary, "erase", workspace.demo_paths[0],
            "--allow-file", "--execute", "--confirm", workspace.demo_paths[0], "--verify", NULL);
        char *output = collect(process, i < 2);
        g_assert_nonnull(strstr(output, messages[i]));
        if (i >= 2) g_assert_null(strstr(output, "Erase completed"));
        if (i < 2) check_pattern(0);
        if (i == 9) check_pattern(0xa5);
        g_free(output); g_object_unref(launcher);
    }
}

static void test_patterns(void)
{
    reset_file();
    char *output = collect(start((Settings){"ones", 2, TRUE}, NULL), TRUE);
    g_assert_nonnull(strstr(output, "OMS_PROGRESS verifying 2 2"));
    check_pattern(0xff); g_free(output);
    reset_file(); g_free(collect(start((Settings){"random", 1, FALSE}, NULL), TRUE));
    char *contents; gsize length;
    g_assert_true(g_file_get_contents(workspace.demo_paths[0], &contents, &length, NULL));
    g_assert_cmpuint(length, ==, 1024 * 1024 + 73);
    gboolean changed = FALSE;
    for (gsize i = 0; i < length; ++i) changed |= (unsigned char)contents[i] != 0xa5;
    g_assert_true(changed); g_free(contents);
}

static void test_inspection_timeout(void)
{
    char *stub = g_build_filename(workspace.demo_dir, "stalled-inspector", NULL);
    g_assert_true(g_file_set_contents(stub, "#!/bin/sh\nexec sleep 30\n", -1, NULL));
    g_assert_cmpint(g_chmod(stub, 0700), ==, 0);
    char *saved = workspace.binary;
    workspace.binary = stub;
    GError *error = NULL;
    gint64 started = g_get_monotonic_time();
    g_assert_null(workspace_inventory(&workspace, &error));
    g_assert_nonnull(error);
    g_assert_nonnull(strstr(error->message, "timed out"));
    g_assert_cmpint(g_get_monotonic_time() - started, <, 5 * G_TIME_SPAN_SECOND);
    g_clear_error(&error);
    workspace.binary = saved;
    g_unlink(stub); g_free(stub);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    binary = g_canonicalize_filename("build/oms", NULL);
    fault_binary = g_canonicalize_filename("build/oms-faults", NULL);
    g_assert_true(workspace_init(&workspace, binary, TRUE, NULL));
    g_test_add_func("/native/reports", test_reports);
    g_test_add_func("/native/validation", test_validation);
    g_test_add_func("/native/file-guards", test_file_guards);
    g_test_add_func("/native/io-faults", test_faults);
    g_test_add_func("/native/patterns", test_patterns);
    g_test_add_func("/native/inspection-timeout", test_inspection_timeout);
    int result = g_test_run();
    if (target) json_object_unref(target);
    workspace_clear(&workspace); g_free(binary); g_free(fault_binary);
    return result;
}
