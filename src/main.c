#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aur_source.h"
#include "fuzzy.h"
#include "package.h"
#include "pacman_source.h"
#include "theme.h"

#define AUR_CACHE_MAX_AGE                                                      \
  (24L * 60 * 60)                      /* matches lazy-search's own default */
#define MAX_RENDERED_ROWS 300          /* perf cap -- see note in chat */
#define MAX_AUR_FETCHES_PER_REFRESH 15 /* cap fetches queued per keystroke */

typedef struct {
  size_t idx;
  int score;
} ScoredPkg;

typedef struct {
  char name[NAME_MAX_LEN];
  int is_install; /* 1 = install, 0 = remove */
} PendingAction;

typedef struct {
  GtkWidget *dialog_window;
  char pkg_name[NAME_MAX_LEN];
} ActionContext;

static GtkWidget *main_window;
static GtkWidget *search_entry;
static GtkListBox *package_list;

static PackageList all_packages;
static char **installed_names;
static size_t installed_count;
static size_t aur_fetches_queued_this_pass;

static void refresh_list_view(const char *filter_text);
static void load_installed_names(void);
static int is_installed(const char *name);
static const char *current_filter_text(void);
static void show_error_window(const char *msg);
static void request_aur_description(Package *pkg);

/* ---------- installed-package lookup ---------- */

static int cmp_str(const void *a, const void *b) {
  const char *const *pa = a;
  const char *const *pb = b;
  return strcmp(*pa, *pb);
}

static void free_installed_names(void) {
  if (!installed_names)
    return;
  for (size_t i = 0; i < installed_count; i++)
    free(installed_names[i]);
  free(installed_names);
  installed_names = NULL;
  installed_count = 0;
}

static void load_installed_names(void) {
  free_installed_names();

  FILE *fp = popen("pacman -Q", "r");
  if (!fp)
    return;

  size_t cap = 1024;
  installed_names = malloc(cap * sizeof(char *));

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    char name[NAME_MAX_LEN] = {0};
    sscanf(line, "%127s", name); /* first whitespace-separated token */
    if (installed_count == cap) {
      cap *= 2;
      installed_names = realloc(installed_names, cap * sizeof(char *));
    }
    installed_names[installed_count++] = strdup(name);
  }
  pclose(fp);

  qsort(installed_names, installed_count, sizeof(char *), cmp_str);
}

static int is_installed(const char *name) {
  if (!installed_names)
    return 0;
  const char *key = name;
  void *found =
      bsearch(&key, installed_names, installed_count, sizeof(char *), cmp_str);
  return found != NULL;
}

/* ---------- sorting helpers ---------- */

static int compare_scored_desc(const void *a, const void *b) {
  const ScoredPkg *pa = a;
  const ScoredPkg *pb = b;
  if (pb->score != pa->score)
    return pb->score - pa->score;
  return strcmp(all_packages.items[pa->idx].name,
                all_packages.items[pb->idx].name);
}

static int compare_scored_alpha(const void *a, const void *b) {
  const ScoredPkg *pa = a;
  const ScoredPkg *pb = b;
  return strcmp(all_packages.items[pa->idx].name,
                all_packages.items[pb->idx].name);
}

/* ---------- dialogs ---------- */

static void show_error_window(const char *msg) {
  GtkWidget *win = gtk_window_new();
  gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
  gtk_window_set_modal(GTK_WINDOW(win), TRUE);
  gtk_window_set_title(GTK_WINDOW(win), "Error");
  gtk_window_set_default_size(GTK_WINDOW(win), 420, 150);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(vbox, 12);
  gtk_widget_set_margin_end(vbox, 12);
  gtk_widget_set_margin_top(vbox, 12);
  gtk_widget_set_margin_bottom(vbox, 12);

  GtkWidget *label = gtk_label_new(msg);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_box_append(GTK_BOX(vbox), label);

  GtkWidget *ok_btn = gtk_button_new_with_label("OK");
  g_signal_connect_swapped(ok_btn, "clicked", G_CALLBACK(gtk_window_destroy),
                           win);
  gtk_box_append(GTK_BOX(vbox), ok_btn);

  gtk_window_set_child(GTK_WINDOW(win), vbox);
  gtk_window_present(GTK_WINDOW(win));
}

/* ---------- async pacman actions (never block the GTK main loop) ---------- */

static void on_pacman_finished(GObject *source, GAsyncResult *res,
                               gpointer user_data) {
  GSubprocess *proc = G_SUBPROCESS(source);
  ActionContext *ctx = user_data;

  gchar *out_buf = NULL, *err_buf = NULL;
  GError *error = NULL;
  gboolean communicated = g_subprocess_communicate_utf8_finish(
      proc, res, &out_buf, &err_buf, &error);
  gboolean success = communicated && g_subprocess_get_successful(proc);

  if (ctx->dialog_window)
    gtk_window_destroy(GTK_WINDOW(ctx->dialog_window));

  if (success) {
    load_installed_names();
    refresh_list_view(current_filter_text());
  } else {
    const char *msg = (err_buf && *err_buf)
                          ? err_buf
                          : (error ? error->message : "pacman failed");
    show_error_window(msg);
  }

  g_free(out_buf);
  g_free(err_buf);
  g_clear_error(&error);
  g_object_unref(proc);
  g_free(ctx);
}

static void run_pacman_action_async(const char *verb, const char *name,
                                    GtkWidget *dialog_window) {
  GError *error = NULL;
  GSubprocess *proc = g_subprocess_new(
      G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
      &error, "pkexec", "pacman", verb, "--noconfirm", name, NULL);

  if (!proc) {
    if (dialog_window)
      gtk_window_destroy(GTK_WINDOW(dialog_window));
    show_error_window(error ? error->message : "spawn failed");
    g_clear_error(&error);
    return;
  }

  ActionContext *ctx = g_new0(ActionContext, 1);
  ctx->dialog_window = dialog_window;
  g_strlcpy(ctx->pkg_name, name, sizeof(ctx->pkg_name));

  g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_pacman_finished,
                                      ctx);
}

static void on_action_confirmed(GtkButton *btn, gpointer user_data) {
  PendingAction *pa = user_data;
  GtkWidget *dialog_window =
      gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW);

  /* Disable the button immediately so the click visibly registers
     while pkexec's auth prompt is up, instead of the window looking dead. */
  gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);

  run_pacman_action_async(pa->is_install ? "-S" : "-R", pa->name,
                          dialog_window);
  g_free(pa);
}

static void on_action_cancelled(GtkButton *btn, gpointer user_data) {
  PendingAction *pa = user_data;
  GtkWidget *dialog_window =
      gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW);
  if (dialog_window)
    gtk_window_destroy(GTK_WINDOW(dialog_window));
  g_free(pa);
}

static void open_confirm_dialog(PendingAction *pa) {
  GtkWidget *win = gtk_window_new();
  gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
  gtk_window_set_modal(GTK_WINDOW(win), TRUE);
  gtk_window_set_title(GTK_WINDOW(win),
                       pa->is_install ? "Confirm Install" : "Confirm Remove");
  gtk_window_set_default_size(GTK_WINDOW(win), 420, 150);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(vbox, 12);
  gtk_widget_set_margin_end(vbox, 12);
  gtk_widget_set_margin_top(vbox, 12);
  gtk_widget_set_margin_bottom(vbox, 12);

  char preview[256];
  snprintf(preview, sizeof(preview), "pkexec pacman %s %s --noconfirm",
           pa->is_install ? "-S" : "-R", pa->name);
  GtkWidget *label = gtk_label_new(preview);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_box_append(GTK_BOX(vbox), label);

  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign(hbox, GTK_ALIGN_END);

  GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
  g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_action_cancelled), pa);
  gtk_box_append(GTK_BOX(hbox), cancel_btn);

  GtkWidget *confirm_btn =
      gtk_button_new_with_label(pa->is_install ? "Install" : "Remove");
  g_signal_connect(confirm_btn, "clicked", G_CALLBACK(on_action_confirmed), pa);
  gtk_box_append(GTK_BOX(hbox), confirm_btn);

  gtk_box_append(GTK_BOX(vbox), hbox);
  gtk_window_set_child(GTK_WINDOW(win), vbox);
  gtk_window_present(GTK_WINDOW(win));
}

static void on_row_button_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Package *pkg = user_data; /* stable pointer into all_packages.items */
  PendingAction *pa = g_new0(PendingAction, 1);
  g_strlcpy(pa->name, pkg->name, sizeof(pa->name));
  pa->is_install = !is_installed(pkg->name);
  open_confirm_dialog(pa);
}

/* ---------- async AUR description fetch ---------- */

#define MAX_CONCURRENT_AUR_FETCHES 4

typedef struct {
  char name[NAME_MAX_LEN];
  Package *pkg;
} AurDescFetchCtx;

static GThreadPool *aur_desc_pool = NULL;

/* Fetch completions can arrive in a burst (many AUR rows finishing
   close together). Rebuilding the whole visible list on every single
   completion is O(rows^2) and freezes the UI, so completions just mark
   a "refresh needed" flag and a single throttled timeout actually does
   the rebuild, at most twice a second no matter how many finish. */
static guint list_refresh_timeout_id = 0;

static gboolean do_scheduled_refresh(gpointer user_data) {
  (void)user_data;
  list_refresh_timeout_id = 0;
  refresh_list_view(current_filter_text());
  return G_SOURCE_REMOVE;
}

static void schedule_list_refresh(void) {
  if (list_refresh_timeout_id != 0)
    return;
  list_refresh_timeout_id = g_timeout_add(500, do_scheduled_refresh, NULL);
}

static gboolean aur_desc_fetch_done(gpointer user_data) {
  AurDescFetchCtx *ctx = user_data;
  ctx->pkg->fetching = 0;
  schedule_list_refresh();
  g_free(ctx);
  return G_SOURCE_REMOVE;
}

static void aur_desc_pool_func(gpointer data, gpointer user_data) {
  (void)user_data;
  AurDescFetchCtx *ctx = data;
  fprintf(stderr, "[aur-fetch] running: %s\n", ctx->name);
  char desc[DESC_MAX_LEN] = {0};
  if (aur_fetch_description(ctx->name, desc, sizeof(desc)) == 0) {
    g_strlcpy(ctx->pkg->description, desc, sizeof(ctx->pkg->description));
    ctx->pkg->has_desc = 1;
    fprintf(stderr, "[aur-fetch] success: %s\n", ctx->name);
  } else {
    fprintf(stderr, "[aur-fetch] failed: %s\n", ctx->name);
  }
  g_idle_add(aur_desc_fetch_done, ctx);
}

static void request_aur_description(Package *pkg) {
  if (pkg->has_desc || pkg->fetching)
    return;
  pkg->fetching = 1;
  fprintf(stderr, "[aur-fetch] queuing: %s\n", pkg->name);

  if (!aur_desc_pool) {
    aur_desc_pool = g_thread_pool_new(aur_desc_pool_func, NULL,
                                      MAX_CONCURRENT_AUR_FETCHES, FALSE, NULL);
  }

  AurDescFetchCtx *ctx = g_new0(AurDescFetchCtx, 1);
  g_strlcpy(ctx->name, pkg->name, sizeof(ctx->name));
  ctx->pkg = pkg;

  g_thread_pool_push(aur_desc_pool, ctx, NULL);
}

/* ---------- list rendering ---------- */

static const char *current_filter_text(void) {
  return gtk_editable_get_text(GTK_EDITABLE(search_entry));
}

static void refresh_list_view(const char *filter_text) {
  GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(package_list));
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(package_list, child);
    child = next;
  }

  aur_fetches_queued_this_pass = 0;

  size_t n = all_packages.count;
  ScoredPkg *scored = malloc(n * sizeof(ScoredPkg));
  if (!scored)
    return;
  size_t sn = 0;

  gboolean has_filter = filter_text && *filter_text;

  for (size_t i = 0; i < n; i++) {
    int score = 0;
    if (has_filter) {
      score = fuzzy_score(filter_text, all_packages.items[i].name);
      if (score < 0)
        continue;
    }
    scored[sn].idx = i;
    scored[sn].score = score;
    sn++;
  }

  qsort(scored, sn, sizeof(ScoredPkg),
        has_filter ? compare_scored_desc : compare_scored_alpha);

  size_t shown = sn < MAX_RENDERED_ROWS ? sn : MAX_RENDERED_ROWS;

  for (size_t k = 0; k < shown; k++) {
    Package *pkg = &all_packages.items[scored[k].idx];
    int inst = is_installed(pkg->name);

    if (has_filter && strcmp(pkg->repo, "aur") == 0 &&
        aur_fetches_queued_this_pass < MAX_AUR_FETCHES_PER_REFRESH) {
      request_aur_description(pkg);
      aur_fetches_queued_this_pass++;
    }

    /* --- card container --- */
    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(row_box, 10);
    gtk_widget_set_margin_end(row_box, 10);
    gtk_widget_set_margin_top(row_box, 8);
    gtk_widget_set_margin_bottom(row_box, 8);
    gtk_widget_add_css_class(row_box, "package-row");

    /* --- icon --- */
    GtkWidget *icon = gtk_image_new_from_icon_name("package-x-generic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_box_append(GTK_BOX(row_box), icon);

    /* --- name + version/repo + description stacked --- */
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text_box, TRUE);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

    char name_markup[NAME_MAX_LEN + VERSION_MAX_LEN + REPO_MAX_LEN + 32];
    snprintf(name_markup, sizeof(name_markup),
             "<b>%s</b>  <small>%s [%s]</small>", pkg->name, pkg->version,
             pkg->repo);
    GtkWidget *name_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(name_label), name_markup);
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    gtk_box_append(GTK_BOX(text_box), name_label);

    const char *desc_text = pkg->has_desc ? pkg->description : "";
    GtkWidget *desc_label = gtk_label_new(desc_text);
    gtk_label_set_xalign(GTK_LABEL(desc_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(desc_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(desc_label), 60);
    gtk_widget_add_css_class(desc_label, "dim-label");
    gtk_box_append(GTK_BOX(text_box), desc_label);

    gtk_box_append(GTK_BOX(row_box), text_box);

    /* --- install/remove button --- */
    GtkWidget *btn = gtk_button_new_with_label(inst ? "Remove" : "Install");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_row_button_clicked), pkg);
    gtk_box_append(GTK_BOX(row_box), btn);

    gtk_list_box_append(package_list, row_box);
  }

  free(scored);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
  (void)user_data;
  const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
  refresh_list_view(text);
}

/* ---------- app setup ---------- */

static void activate(GtkApplication *app, gpointer user_data) {
  (void)user_data;
  main_window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(main_window), "Lazy Store");
  gtk_window_set_default_size(GTK_WINDOW(main_window), 900, 600);

  apply_dynamic_theme(main_window);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_start(vbox, 10);
  gtk_widget_set_margin_end(vbox, 10);
  gtk_widget_set_margin_top(vbox, 10);
  gtk_widget_set_margin_bottom(vbox, 10);

  search_entry = gtk_search_entry_new();
  gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search_entry),
                                        "Search packages...");
  g_signal_connect(search_entry, "search-changed",
                   G_CALLBACK(on_search_changed), NULL);
  gtk_box_append(GTK_BOX(vbox), search_entry);

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, TRUE);

  package_list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_list_box_set_selection_mode(package_list, GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                GTK_WIDGET(package_list));
  gtk_box_append(GTK_BOX(vbox), scroll);

  gtk_window_set_child(GTK_WINDOW(main_window), vbox);

  pkglist_init(&all_packages);
  pacman_load_packages(&all_packages);
  aur_load_packages(&all_packages, AUR_CACHE_MAX_AGE);
  pacman_bulk_load_descriptions(&all_packages);

  load_installed_names();
  refresh_list_view(NULL);

  gtk_window_present(GTK_WINDOW(main_window));
}

int main(int argc, char **argv) {
  GtkApplication *app =
      gtk_application_new("com.lazyos.lazystore", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
