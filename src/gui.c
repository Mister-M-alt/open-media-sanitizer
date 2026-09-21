// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "workspace.h"
#include "oms.h"
#include <gtk/gtk.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    Workspace workspace;
    GtkWidget *window, *stack, *inventory, *method, *passes, *verify, *review, *confirm;
    GtkWidget *execute, *cancel, *progress, *status, *logview, *reports, *refresh, *review_button;
    GtkListStore *media, *records;
    JsonArray *targets;
    GPtrArray *history;
    JsonObject *target;
    Settings settings;
    GSubprocess *process;
    GDataInputStream *stream;
    GString *log;
    char *started;
    gint64 clock;
    gboolean cancelled, closing, stream_failed;
} App;

static void show_error(App *app, const char *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static GtkWidget *label(const char *text, const char *style)
{
    GtkWidget *widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
    if (style) gtk_style_context_add_class(gtk_widget_get_style_context(widget), style);
    return widget;
}

static GtkWidget *page(App *app, const char *name, const char *title, const char *subtitle)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_container_set_border_width(GTK_CONTAINER(box), 28);
    gtk_box_pack_start(GTK_BOX(box), label(title, "title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label(subtitle, "muted"), FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(app->stack), box, name, title);
    return box;
}

static GtkWidget *button(GtkWidget *box, const char *text, GCallback callback, App *app)
{
    GtkWidget *widget = gtk_button_new_with_label(text);
    gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
    if (callback) g_signal_connect(widget, "clicked", callback, app);
    return widget;
}

static GtkWidget *table(GtkWidget *box, GtkListStore *model, const char *const *titles, int columns)
{
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    for (int i = 0; i < columns; ++i) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", i, NULL);
        gtk_tree_view_column_set_min_width(column, i == 0 ? 180 : 80);
        gtk_tree_view_column_set_expand(column, i == 0);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_widget_set_size_request(scroll, -1, 140);
    return view;
}

static int selected_index(GtkWidget *view, int column)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    int index = -1;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), &model, &iter))
        gtk_tree_model_get(model, &iter, column, &index, -1);
    return index;
}

static void refresh(GtkWidget *widget, App *app)
{
    (void)widget;
    if (app->process) return;
    GError *error = NULL;
    JsonArray *targets = workspace_inventory(&app->workspace, &error);
    if (!targets) { show_error(app, error->message); g_clear_error(&error); return; }
    if (app->targets) json_array_unref(app->targets);
    app->targets = targets;
    gtk_list_store_clear(app->media);
    for (unsigned i = 0; i < json_array_get_length(targets); ++i) {
        JsonObject *target = json_array_get_object_element(targets, i);
        char *size = g_format_size_full(json_object_get_int_member(target, "size_bytes"), G_FORMAT_SIZE_IEC_UNITS);
        const char *reason = target_blocked(target);
        GtkTreeIter iter;
        gtk_list_store_insert_with_values(app->media, &iter, -1,
            0, json_object_get_string_member(target, "path"),
            1, json_object_get_string_member(target, "model"), 2, size, 3, reason ? reason : "Available", 4, (int)i, -1);
        g_free(size);
    }
}

static void method_changed(GtkComboBox *widget, App *app)
{
    gboolean can_verify = gtk_combo_box_get_active(widget) != 2;
    if (!can_verify) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->verify), FALSE);
    gtk_widget_set_sensitive(app->verify, can_verify && !app->process);
}

static void confirmation_changed(GtkEditable *widget, App *app)
{
    (void)widget;
    gtk_widget_set_sensitive(app->execute, app->target && !app->process &&
        !strcmp(gtk_entry_get_text(GTK_ENTRY(app->confirm)), json_object_get_string_member(app->target, "path")));
}

static void review(GtkWidget *widget, App *app)
{
    (void)widget;
    if (app->process) return;
    int index = selected_index(app->inventory, 4);
    if (index < 0) { show_error(app, "Select a medium in Storage first."); return; }
    JsonObject *target = json_array_get_object_element(app->targets, (unsigned)index);
    const char *reason = target_blocked(target);
    if (reason) { show_error(app, reason); return; }
    if (!app->workspace.demo && geteuid() != 0) {
        show_error(app, "Physical erasure requires running the application as root. The bootable edition runs locally with the required access.");
        return;
    }
    if (app->target) json_object_unref(app->target);
    app->target = json_object_ref(target);
    static const char *methods[] = {"zero", "ones", "random"};
    app->settings = (Settings){methods[gtk_combo_box_get_active(GTK_COMBO_BOX(app->method))],
        (unsigned)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->passes)),
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->verify))};
    char *size = g_format_size_full(json_object_get_int_member(target, "size_bytes"), G_FORMAT_SIZE_IEC_UNITS);
    char *text = g_strdup_printf("%s\n%s  ·  %s\n\nPattern: %s   Passes: %u   Read-back: %s\n\n"
        "All exposed data on this target will be overwritten.\nType the full path below to authorize this operation.",
        json_object_get_string_member(target, "path"), json_object_get_string_member(target, "model"), size,
        app->settings.method, app->settings.passes, app->settings.verify ? "enabled" : "disabled");
    gtk_label_set_text(GTK_LABEL(app->review), text);
    gtk_entry_set_text(GTK_ENTRY(app->confirm), "");
    confirmation_changed(NULL, app);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "review");
    gtk_widget_grab_focus(app->confirm);
    g_free(size); g_free(text);
}

static void append_log(App *app, const char *line)
{
    char *valid = g_utf8_make_valid(line, -1);
    g_string_append_printf(app->log, "%s\n", valid);
    g_free(valid);
    /* Keep memory bounded even if a failing device emits extensive diagnostics. */
    while (app->log->len > 65536) {
        char *newline = strchr(app->log->str, '\n');
        if (!newline) { g_string_truncate(app->log, 0); break; }
        g_string_erase(app->log, 0, newline - app->log->str + 1);
    }
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->logview));
    gtk_text_buffer_set_text(buffer, app->log->str, -1);
}

static void set_busy(App *app, gboolean busy)
{
    gtk_widget_set_sensitive(app->refresh, !busy);
    gtk_widget_set_sensitive(app->review_button, !busy);
    gtk_widget_set_sensitive(app->method, !busy);
    gtk_widget_set_sensitive(app->passes, !busy);
    gtk_widget_set_sensitive(app->confirm, !busy);
    gtk_widget_set_sensitive(app->cancel, busy);
    method_changed(GTK_COMBO_BOX(app->method), app);
    confirmation_changed(NULL, app);
}

static void add_record(App *app, const char *outcome, int code)
{
    JsonObject *record = report_new(app->target, app->settings, app->workspace.demo, app->started,
        app->clock, outcome, code, app->log->str);
    g_ptr_array_add(app->history, record);
    GtkTreeIter iter;
    gtk_list_store_insert_with_values(app->records, &iter, -1,
        0, json_object_get_string_member(record, "finished_at"), 1, json_object_get_string_member(app->target, "path"),
        2, outcome, 3, (int)app->history->len - 1, -1);
    gtk_label_set_text(GTK_LABEL(app->status), outcome);
}

static void finished(GObject *source, GAsyncResult *result, gpointer data)
{
    App *app = data;
    GError *error = NULL;
    gboolean waited = g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
    int code = waited && g_subprocess_get_if_exited(app->process) ? g_subprocess_get_exit_status(app->process) : -1;
    if (error) { append_log(app, error->message); g_clear_error(&error); }
    const char *outcome = waited && code == 0 && !app->stream_failed ? "completed" : (app->cancelled ? "cancelled" : "failed");
    add_record(app, outcome, code);
    if (!strcmp(outcome, "completed")) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 1);
    g_clear_object(&app->stream);
    g_clear_object(&app->process);
    g_clear_pointer(&app->started, g_free);
    gtk_entry_set_text(GTK_ENTRY(app->confirm), "");
    set_busy(app, FALSE);
    if (app->closing) gtk_main_quit();
}

static void read_line(GObject *source, GAsyncResult *result, gpointer data)
{
    App *app = data;
    GError *error = NULL;
    char *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result, NULL, &error);
    if (!line) {
        if (error) {
            app->stream_failed = TRUE;
            append_log(app, error->message); g_clear_error(&error);
            g_subprocess_send_signal(app->process, SIGTERM);
        }
        g_subprocess_wait_async(app->process, NULL, finished, app);
        return;
    }
    char phase[24];
    unsigned pass, passes;
    guint64 done, total;
    if (sscanf(line, "OMS_PROGRESS %23s %u %u %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT,
               phase, &pass, &passes, &done, &total) == 5 && total > 0) {
        char *text = g_strdup_printf("%s · pass %u of %u", phase, pass, passes);
        gtk_label_set_text(GTK_LABEL(app->status), text);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), MIN(1.0, (double)done / (double)total));
        g_free(text);
    } else append_log(app, line);
    g_free(line);
    g_data_input_stream_read_line_async(app->stream, G_PRIORITY_DEFAULT, NULL, read_line, app);
}

static void execute(GtkWidget *widget, App *app)
{
    (void)widget;
    if (app->process || !app->target) return;
    GError *error = NULL;
    app->cancelled = app->stream_failed = FALSE;
    app->started = utc_now();
    app->clock = g_get_monotonic_time();
    g_string_truncate(app->log, 0);
    app->process = workspace_start(&app->workspace, app->target, app->settings,
        gtk_entry_get_text(GTK_ENTRY(app->confirm)), &error);
    if (!app->process) {
        append_log(app, error->message); g_clear_error(&error);
        add_record(app, "failed", -1);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "activity");
        gtk_entry_set_text(GTK_ENTRY(app->confirm), "");
        g_clear_pointer(&app->started, g_free);
        return;
    }
    app->stream = g_data_input_stream_new(g_subprocess_get_stdout_pipe(app->process));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 0);
    gtk_label_set_text(GTK_LABEL(app->status), "Starting operation…");
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "activity");
    set_busy(app, TRUE);
    g_data_input_stream_read_line_async(app->stream, G_PRIORITY_DEFAULT, NULL, read_line, app);
}

static void cancel(GtkWidget *widget, App *app)
{
    (void)widget;
    if (!app->process) return;
    app->cancelled = TRUE;
    gtk_widget_set_sensitive(app->cancel, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status), "Stopping and flushing writes…");
    g_subprocess_send_signal(app->process, SIGTERM);
}

static gboolean close_window(GtkWidget *widget, GdkEvent *event, App *app)
{
    (void)widget; (void)event;
    if (!app->process) { gtk_main_quit(); return TRUE; }
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Stop the operation and close after writes have been flushed?");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Keep running", GTK_RESPONSE_CANCEL, "Stop and close", GTK_RESPONSE_ACCEPT, NULL);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_ACCEPT) { app->closing = TRUE; cancel(NULL, app); }
    return TRUE;
}

static void export_report(GtkWidget *widget, App *app)
{
    int index = selected_index(app->reports, 3);
    if (index < 0) { show_error(app, "Select an operation record first."); return; }
    gboolean html = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "html"));
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Export operation record", GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), html ? "operation.html" : "operation.json");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        GError *error = NULL;
        if (!report_save(g_ptr_array_index(app->history, (unsigned)index), path, html, &error)) {
            show_error(app, error->message); g_clear_error(&error);
        }
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}

static void build_window(App *app, gboolean fullscreen)
{
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Open Media Sanitizer — Media Workspace");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1120, 760);
    g_signal_connect(app->window, "delete-event", G_CALLBACK(close_window), app);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), outer);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(header), 20);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "header");
    gtk_box_pack_start(GTK_BOX(header), label("OPEN MEDIA SANITIZER", "brand"), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(header), label(app->workspace.demo ? "DEMO · disposable files" : "LIVE · local devices", "badge"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(outer), body, TRUE, TRUE, 0);
    app->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget *sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(app->stack));
    gtk_widget_set_size_request(sidebar, 180, -1);
    gtk_box_pack_start(GTK_BOX(body), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), app->stack, TRUE, TRUE, 0);

    GtkWidget *storage = page(app, "storage", "Storage", "Choose one medium, configure a write pattern, then review the operation.");
    app->media = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    const char *columns[] = {"Target", "Model", "Capacity", "Status"};
    app->inventory = table(storage, app->media, columns, 4);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(app->inventory), 3);
    GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(storage), settings, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(settings), label("Write pattern", NULL), FALSE, FALSE, 0);
    app->method = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->method), "Zeroes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->method), "Ones");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->method), "Random");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->method), 0);
    gtk_box_pack_start(GTK_BOX(settings), app->method, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(settings), label("Passes", NULL), FALSE, FALSE, 0);
    app->passes = gtk_spin_button_new_with_range(1, 16, 1);
    gtk_box_pack_start(GTK_BOX(settings), app->passes, FALSE, FALSE, 0);
    app->verify = gtk_check_button_new_with_label("Verify read-back");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->verify), TRUE);
    gtk_box_pack_start(GTK_BOX(settings), app->verify, FALSE, FALSE, 0);
    g_signal_connect(app->method, "changed", G_CALLBACK(method_changed), app);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(storage), actions, FALSE, FALSE, 0);
    app->refresh = button(actions, "Refresh devices", G_CALLBACK(refresh), app);
    app->review_button = button(actions, "Review operation →", G_CALLBACK(review), app);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->review_button), "suggested-action");
    GtkWidget *review_page = page(app, "review", "Review", "Confirmation applies to the selected device identity and capacity.");
    app->review = label("Select a medium in Storage to prepare an operation.", NULL);
    gtk_label_set_selectable(GTK_LABEL(app->review), TRUE);
    gtk_box_pack_start(GTK_BOX(review_page), app->review, TRUE, TRUE, 0);
    app->confirm = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->confirm), "Type the complete target path");
    gtk_box_pack_start(GTK_BOX(review_page), app->confirm, FALSE, FALSE, 0);
    g_signal_connect(app->confirm, "changed", G_CALLBACK(confirmation_changed), app);
    app->execute = button(review_page, "Overwrite this medium", G_CALLBACK(execute), app);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->execute), "destructive-action");
    GtkWidget *activity = page(app, "activity", "Activity", "Follow writes and verification. Cancellation leaves an incomplete operation.");
    app->status = label("No operation running", "title");
    gtk_box_pack_start(GTK_BOX(activity), app->status, FALSE, FALSE, 0);
    app->progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(activity), app->progress, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    app->logview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->logview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->logview), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->logview), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scroll), app->logview);
    gtk_box_pack_start(GTK_BOX(activity), scroll, TRUE, TRUE, 0);
    app->cancel = button(activity, "Stop operation", G_CALLBACK(cancel), app);
    GtkWidget *reports = page(app, "reports", "Reports", "Session records are kept in memory. Export the records you want to retain.");
    app->records = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    const char *report_columns[] = {"Finished (UTC)", "Target", "Outcome"};
    app->reports = table(reports, app->records, report_columns, 3);
    GtkWidget *exports = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(reports), exports, FALSE, FALSE, 0);
    button(exports, "Export JSON", G_CALLBACK(export_report), app);
    GtkWidget *html = button(exports, "Export printable HTML", G_CALLBACK(export_report), app);
    g_object_set_data(G_OBJECT(html), "html", GINT_TO_POINTER(TRUE));
    GtkWidget *about = page(app, "about", "About", "A local workspace for deliberate media operations.");
    gtk_box_pack_start(GTK_BOX(about), label("Open Media Sanitizer " OMS_VERSION "\n\n"
        "Native C application. No account or network service required.\n\n"
        "Source code: MIT OR Apache-2.0. System libraries retain their own licenses.\n\n"
        "Writes cover the exposed logical address range. Hidden, remapped and overprovisioned storage are outside this implementation. "
        "Read-back is not proof of physical sanitization.", NULL), FALSE, FALSE, 0);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window{background:#edf2f6;color:#142a38} .header{background:#142a38;color:#fff}"
        ".brand{font-weight:bold;letter-spacing:2px}.badge{color:#65d5c5}.title{font-size:26px;font-weight:bold}"
        ".muted{color:#526b79}stacksidebar{background:#203947;color:#e3edf3}stacksidebar row:selected{background:#087f75}"
        "button{padding:10px 16px}button.suggested-action{background:#087f75;color:white}"
        "treeview{background:white;color:#142a38}treeview:selected{background:#c9e9e4;color:#142a38}"
        "entry{padding:10px}progressbar progress{background:#087f75}", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "storage");
    set_busy(app, FALSE);
    gtk_widget_show_all(app->window);
    if (fullscreen) gtk_window_fullscreen(GTK_WINDOW(app->window));
}

static void app_clear(App *app)
{
    gtk_widget_destroy(app->window);
    if (app->targets) json_array_unref(app->targets);
    if (app->target) json_object_unref(app->target);
    g_object_unref(app->media); g_object_unref(app->records);
    g_ptr_array_unref(app->history);
    g_string_free(app->log, TRUE);
    workspace_clear(&app->workspace);
}

int main(int argc, char **argv)
{
    gboolean demo = FALSE, fullscreen = FALSE;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--demo")) demo = TRUE;
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = TRUE;
        else if (!strcmp(argv[i], "--help")) { puts("oms-gui [--demo] [--fullscreen]"); return 0; }
        else { fprintf(stderr, "Unknown argument: %s\n", argv[i]); return 2; }
    }
    if (!gtk_init_check(NULL, NULL)) {
        fputs("No graphical display is available. Start a graphical session or use the oms CLI.\n", stderr); return 1;
    }
    /* Resolve beside our executable, never through an untrusted PATH when root. */
    char executable[OMS_PATH_CAP];
    ssize_t count = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (count < 0 || (size_t)count >= sizeof(executable) - 1) return 1;
    executable[count] = '\0';
    char *directory = g_path_get_dirname(executable);
    char *binary = g_build_filename(directory, "oms", NULL);
    App app = {.history = g_ptr_array_new_with_free_func((GDestroyNotify)json_object_unref), .log = g_string_new(NULL)};
    GError *error = NULL;
    if (!workspace_init(&app.workspace, binary, demo, &error)) {
        fprintf(stderr, "%s\n", error->message); g_error_free(error);
        workspace_clear(&app.workspace);
        g_ptr_array_unref(app.history); g_string_free(app.log, TRUE);
        g_free(directory); g_free(binary); return 1;
    }
    g_free(directory); g_free(binary);
    build_window(&app, fullscreen);
    refresh(NULL, &app);
    gtk_main();
    app_clear(&app);
    return 0;
}
