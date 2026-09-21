// SPDX-License-Identifier: Apache-2.0 OR MIT
#ifndef OMS_WORKSPACE_H
#define OMS_WORKSPACE_H
#include <gio/gio.h>
#include <json-glib/json-glib.h>

typedef struct {
    char *binary, *demo_dir, *demo_paths[2];
    gboolean demo;
} Workspace;

typedef struct { const char *method; unsigned passes; gboolean verify; } Settings;

gboolean workspace_init(Workspace *, const char *, gboolean, GError **);
void workspace_clear(Workspace *);
JsonArray *workspace_inventory(Workspace *, GError **);
const char *target_blocked(JsonObject *);
GSubprocess *workspace_start(Workspace *, JsonObject *, Settings, const char *, GError **);
JsonObject *report_new(JsonObject *, Settings, gboolean, const char *, gint64,
                       const char *, int, const char *);
char *report_render(JsonObject *, gboolean);
gboolean report_save(JsonObject *, const char *, gboolean, GError **);
char *utc_now(void);
#endif
