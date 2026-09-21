// SPDX-License-Identifier: Apache-2.0 OR MIT
#define main application_main
#include "../src/gui.c"
#undef main

static void test_workflow(void)
{
    App app = {.history = g_ptr_array_new_with_free_func((GDestroyNotify)json_object_unref), .log = g_string_new(NULL)};
    char *binary = g_canonicalize_filename("build/oms", NULL);
    g_assert_true(workspace_init(&app.workspace, binary, TRUE, NULL)); g_free(binary);
    build_window(&app, FALSE); refresh(NULL, &app);
    g_assert_cmpuint(json_array_get_length(app.targets), ==, 2);
    g_assert_false(gtk_widget_get_sensitive(app.execute));
    GtkTreePath *path = gtk_tree_path_new_from_indices(0, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.inventory)), path);
    gtk_tree_path_free(path);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.method), 2);
    g_assert_false(gtk_widget_get_sensitive(app.verify));
    g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.verify)));
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.method), 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.verify), TRUE);
    review(NULL, &app);
    gtk_entry_set_text(GTK_ENTRY(app.confirm), "wrong");
    g_assert_false(gtk_widget_get_sensitive(app.execute));
    gtk_entry_set_text(GTK_ENTRY(app.confirm), app.workspace.demo_paths[0]);
    g_assert_true(gtk_widget_get_sensitive(app.execute));
    execute(NULL, &app);
    g_assert_nonnull(app.process);
    g_assert_false(gtk_widget_get_sensitive(app.review_button));
    gint64 deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
    while (app.process && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    }
    g_assert_null(app.process);
    g_assert_cmpuint(app.history->len, ==, 1);
    JsonObject *record = g_ptr_array_index(app.history, 0);
    g_assert_cmpstr(json_object_get_string_member(record, "outcome"), ==, "completed");
    g_assert_true(json_object_get_boolean_member(record, "verification_completed"));
    g_assert_false(gtk_widget_get_sensitive(app.execute));
    g_assert_true(gtk_widget_get_sensitive(app.review_button));
    /* A missing executable becomes an exportable failure, never success. */
    review(NULL, &app);
    gtk_entry_set_text(GTK_ENTRY(app.confirm), app.workspace.demo_paths[0]);
    char *saved_binary = app.workspace.binary;
    app.workspace.binary = g_strdup("/nonexistent/oms");
    execute(NULL, &app);
    g_assert_null(app.process);
    g_assert_cmpuint(app.history->len, ==, 2);
    g_assert_cmpstr(json_object_get_string_member(g_ptr_array_index(app.history, 1), "outcome"), ==, "failed");
    g_free(app.workspace.binary); app.workspace.binary = saved_binary;
    /* Stop a long disposable-file operation and wait for child cleanup. */
    g_assert_cmpint(truncate(app.workspace.demo_paths[0], (off_t)8 * 1024 * 1024 * 1024), ==, 0);
    refresh(NULL, &app);
    path = gtk_tree_path_new_from_indices(0, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.inventory)), path);
    gtk_tree_path_free(path);
    review(NULL, &app);
    gtk_entry_set_text(GTK_ENTRY(app.confirm), app.workspace.demo_paths[0]);
    execute(NULL, &app);
    cancel(NULL, &app);
    deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
    while (app.process && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    }
    g_assert_null(app.process);
    g_assert_cmpuint(app.history->len, ==, 3);
    record = g_ptr_array_index(app.history, 2);
    g_assert_cmpstr(json_object_get_string_member(record, "outcome"), ==, "cancelled");
    g_assert_false(json_object_get_boolean_member(record, "verification_completed"));
    app_clear(&app);
}

int main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add_func("/gui/demo-workflow", test_workflow);
    return g_test_run();
}
