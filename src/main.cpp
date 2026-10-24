// desk-kindl: e-ink desk dashboard for jailbroken Kindles (GTK 2, C++11).
//
// Layout: a header (clock + date) and a grid of "panels". Every panel's
// content comes from a shell command (see config/dashboard.conf), so data
// sources (calendar, weather, transit, server health) are plain
// scripts that print text. This keeps the binary tiny and dependency free.

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <unistd.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "calendar.h"

struct Panel {
    std::string title;
    std::string command;
    int interval = 300;      // seconds between refreshes
    int span = 1;            // columns occupied in the grid
    GtkWidget *body = nullptr;
    GPid pid = 0;            // running child, 0 if idle
    std::string buffer;      // accumulated stdout of the running child
    std::string text = "loading...";  // last published output, kept across relayouts
};

struct Config {
    std::string font = "Sans";
    int columns = 2;
    int title_size = 14;
    int body_size = 12;
    int clock_size = 140;
    std::string orientation = "auto";  // portrait, landscape or auto (follow window shape)
    bool show_titles = false;  // small headings above each panel
    int full_refresh = 600;  // seconds between full e-ink flashes, 0 = never
    bool kindle = false;     // apply Kindle-specific behaviour
    std::string clock_format = "%H:%M";
    std::string date_format = "%A, %d %B %Y";
    std::string timezone = "UTC";
    std::vector<Panel> panels;
};

static Config g_cfg;
static GtkWidget *g_clock = nullptr;
static GtkWidget *g_date = nullptr;

// ---------------------------------------------------------------- config

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool load_config(const std::string &path, Config &cfg) {
    std::ifstream in(path.c_str());
    if (!in) return false;
    std::string line;
    Panel *cur = nullptr;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            std::string sec = trim(line.substr(1, line.find(']') - 1));
            if (sec.compare(0, 6, "panel ") == 0 || sec == "panel") {
                cfg.panels.push_back(Panel());
                cur = &cfg.panels.back();
                cur->title = sec.size() > 6 ? trim(sec.substr(6)) : "";
            } else {
                cur = nullptr;
            }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = line.substr(eq + 1);
        size_t hash = v.find(" #");  // inline comment (but keep '#' inside values)
        if (hash != std::string::npos && k != "command") v = v.substr(0, hash);
        v = trim(v);
        if (cur) {
            if (k == "title") cur->title = v;
            else if (k == "command") cur->command = v;
            else if (k == "interval") cur->interval = std::max(5, atoi(v.c_str()));
            else if (k == "span") cur->span = std::max(1, atoi(v.c_str()));
        } else {
            if (k == "font") cfg.font = v;
            else if (k == "columns") cfg.columns = std::max(1, atoi(v.c_str()));
            else if (k == "title_size") cfg.title_size = atoi(v.c_str());
            else if (k == "body_size") cfg.body_size = atoi(v.c_str());
            else if (k == "clock_size") cfg.clock_size = atoi(v.c_str());
            else if (k == "full_refresh") cfg.full_refresh = atoi(v.c_str());
            else if (k == "kindle") cfg.kindle = (v == "1" || v == "true" || v == "yes");
            else if (k == "clock_format") cfg.clock_format = v;
            else if (k == "date_format") cfg.date_format = v;
            else if (k == "timezone") cfg.timezone = v;
            else if (k == "orientation") cfg.orientation = v;
            else if (k == "show_titles") cfg.show_titles = (v == "1" || v == "true" || v == "yes");
        }
    }
    return true;
}

// ---------------------------------------------------------------- e-ink

// Clear e-ink ghosting by forcing GTK to repaint the whole window. (Calling
// `eips -c` directly would blank the framebuffer behind X's back, leaving
// the screen empty until something else redraws.)
static gboolean full_refresh_cb(gpointer) {
    if (g_cfg.kindle && g_clock) {
        GtkWidget *top = gtk_widget_get_toplevel(g_clock);
        if (top->window) gdk_window_invalidate_rect(top->window, nullptr, TRUE);
    }
    return TRUE;
}

// Ask the Kindle window manager to rotate the screen. U = portrait (up),
// R = landscape. The resulting configure-event rebuilds our layout.
static void kindle_rotate(bool landscape) {
    if (!g_cfg.kindle || g_cfg.orientation == "auto") return;
    g_spawn_command_line_async(landscape
        ? "lipc-set-prop com.lab126.winmgr orientationLock R"
        : "lipc-set-prop com.lab126.winmgr orientationLock U", nullptr);
}

static void kindle_setup() {
    // Keep the device awake while the dashboard runs, restore on exit.
    g_spawn_command_line_async(
        "lipc-set-prop com.lab126.powerd preventScreenSaver 1", nullptr);
    kindle_rotate(g_cfg.orientation == "landscape");
}

static void kindle_teardown() {
    g_spawn_command_line_async(
        "lipc-set-prop com.lab126.powerd preventScreenSaver 0", nullptr);
    g_spawn_command_line_async("lipc-set-prop com.lab126.winmgr orientationLock U", nullptr);
}

// ---------------------------------------------------------------- markup

static std::string markup_text(const char *text, int size, bool bold) {
    char *esc = g_markup_escape_text(text, -1);
    std::ostringstream ss;
    ss << "<span font_desc=\"" << g_cfg.font << (bold ? " Bold " : " ") << size
       << "\">" << esc << "</span>";
    g_free(esc);
    return ss.str();
}

static void set_label(GtkWidget *label, const std::string &text, int size, bool bold) {
    std::string m = markup_text(text.c_str(), size, bold);
    gtk_label_set_markup(GTK_LABEL(label), m.c_str());
}

// Panel output may use simple Pango markup if its first line is "#markup".
static void set_panel_body(Panel &p, const std::string &raw) {
    p.text = raw;
    if (!p.body) return;
    std::string text = raw;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (text.compare(0, 8, "#markup\n") == 0) {
        std::ostringstream ss;
        ss << "<span font_desc=\"" << g_cfg.font << " " << g_cfg.body_size << "\">"
           << text.substr(8) << "</span>";
        // Validate; fall back to plain text on bad markup.
        if (pango_parse_markup(ss.str().c_str(), -1, 0, nullptr, nullptr, nullptr, nullptr)) {
            gtk_label_set_markup(GTK_LABEL(p.body), ss.str().c_str());
            return;
        }
        text = text.substr(8);
    }
    if (text.empty()) text = "(no data)";
    set_label(p.body, text, g_cfg.body_size, false);
}

// ---------------------------------------------------------------- panels

static gboolean on_child_output(GIOChannel *ch, GIOCondition cond, gpointer data) {
    Panel *p = static_cast<Panel *>(data);
    if (cond & G_IO_IN) {
        gchar buf[4096];
        gsize n = 0;
        GIOStatus st = g_io_channel_read_chars(ch, buf, sizeof buf, &n, nullptr);
        if (n > 0) p->buffer.append(buf, n);
        if (st == G_IO_STATUS_NORMAL || st == G_IO_STATUS_AGAIN) return TRUE;
    }
    // EOF or error: publish what we have.
    if (!g_utf8_validate(p->buffer.c_str(), -1, nullptr)) {
        gchar *fixed = g_locale_to_utf8(p->buffer.c_str(), -1, nullptr, nullptr, nullptr);
        p->buffer = fixed ? fixed : "(invalid output)";
        g_free(fixed);
    }
    set_panel_body(*p, p->buffer);
    g_io_channel_shutdown(ch, FALSE, nullptr);
    g_io_channel_unref(ch);

    return FALSE;
}

static void on_child_exit(GPid pid, gint, gpointer data) {
    Panel *p = static_cast<Panel *>(data);
    g_spawn_close_pid(pid);
    p->pid = 0;
}

static gboolean refresh_panel(gpointer data) {
    Panel *p = static_cast<Panel *>(data);
    if (p->pid != 0) return TRUE;  // previous run still busy
    const gchar *argv[] = {"/bin/sh", "-c", p->command.c_str(), nullptr};
    gint out_fd = -1;
    GError *err = nullptr;
    // Merge stderr into stdout so failures show up on the dashboard.
    std::string cmd = "exec 2>&1; " + p->command;
    argv[2] = cmd.c_str();
    if (!g_spawn_async_with_pipes(nullptr, const_cast<gchar **>(argv), nullptr,
                                  G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr,
                                  &p->pid, nullptr, &out_fd, nullptr, &err)) {
        set_panel_body(*p, std::string("error: ") + err->message);
        g_error_free(err);
        p->pid = 0;
        return TRUE;
    }
    p->buffer.clear();
    g_child_watch_add(p->pid, on_child_exit, p);
    GIOChannel *ch = g_io_channel_unix_new(out_fd);
    g_io_channel_set_encoding(ch, nullptr, nullptr);
    g_io_channel_set_close_on_unref(ch, TRUE);
    g_io_add_watch(ch, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), on_child_output, p);
    return TRUE;
}

// ---------------------------------------------------------------- clock

static void draw_clock(GtkWidget *, GtkAllocation *, gpointer) {
    char text[128];
    time_t now = time(nullptr);
    std::tm local = calendar_localtime(now, g_cfg.timezone);
    strftime(text, sizeof text, g_cfg.clock_format.c_str(), &local);
    PangoLayout *layout = gtk_widget_create_pango_layout(g_clock, nullptr);
    std::string markup = markup_text(text, g_cfg.clock_size, false);
    pango_layout_set_markup(layout, markup.c_str(), -1);
    int width = 0;
    pango_layout_get_pixel_size(layout, &width, nullptr);
    g_object_unref(layout);
    int available = std::max(1, g_clock->allocation.width - 16);
    int size = std::max(1, g_cfg.clock_size * std::min(width, available) / std::max(1, width));
    markup = markup_text(text, size, false);
    if (markup != gtk_label_get_label(GTK_LABEL(g_clock)))
        gtk_label_set_markup(GTK_LABEL(g_clock), markup.c_str());
}

static gboolean tick_clock(gpointer) {
    time_t now = time(nullptr);
    std::tm local = calendar_localtime(now, g_cfg.timezone);
    struct tm *lt = &local;
    char buf[128];
    draw_clock(nullptr, nullptr, nullptr);
    strftime(buf, sizeof buf, g_cfg.date_format.c_str(), lt);
    set_label(g_date, buf, g_cfg.title_size + 10, false);
    // Re-arm aligned to the next minute so e-ink updates once per minute.
    g_timeout_add_seconds(60 - lt->tm_sec, tick_clock, nullptr);
    return FALSE;
}

// ---------------------------------------------------------------- UI

static GtkWidget *make_panel_widget(Panel &p) {
    GtkWidget *vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
    if (g_cfg.show_titles && !p.title.empty()) {
        GtkWidget *title = gtk_label_new(nullptr);
        set_label(title, p.title, g_cfg.title_size, true);
        gtk_misc_set_alignment(GTK_MISC(title), 0, 0);
        gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);
    }
    p.body = gtk_label_new(nullptr);
    gtk_misc_set_alignment(GTK_MISC(p.body), 0, 0);
    gtk_label_set_line_wrap(GTK_LABEL(p.body), TRUE);
    set_panel_body(p, p.text);
    gtk_box_pack_start(GTK_BOX(vbox), p.body, TRUE, TRUE, 0);
    return vbox;
}

// ---------------------------------------------------------------- layout

static GtkWidget *g_ebox = nullptr;
static int g_landscape = -1;  // current layout, -1 = not built yet

static bool want_landscape(int width, int height) {
    if (g_cfg.orientation == "landscape") return true;
    if (g_cfg.orientation == "portrait") return false;
    return width > height;
}

// Portrait: clock on top, panels stacked below.
// Landscape: clock on the left, panels in a column on the right.
static void build_layout(bool landscape) {
    if (g_landscape == (int)landscape) return;
    g_landscape = landscape;
    GtkWidget *old = gtk_bin_get_child(GTK_BIN(g_ebox));
    if (old) gtk_widget_destroy(old);
    for (auto &p : g_cfg.panels) p.body = nullptr;

    GtkWidget *root = landscape ? gtk_hbox_new(FALSE, 32) : gtk_vbox_new(FALSE, 24);
    gtk_container_set_border_width(GTK_CONTAINER(root), 32);
    gtk_container_add(GTK_CONTAINER(g_ebox), root);

    // Keep the clock dominant; fit its numerals to the space it gets.
    GtkWidget *header = gtk_vbox_new(FALSE, 0);
    g_clock = gtk_label_new(nullptr);
    gtk_label_set_ellipsize(GTK_LABEL(g_clock), PANGO_ELLIPSIZE_END);
    g_signal_connect(g_clock, "size-allocate", G_CALLBACK(draw_clock), nullptr);
    g_date = gtk_label_new(nullptr);
    gtk_box_pack_start(GTK_BOX(header), g_clock, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), g_date, FALSE, FALSE, 8);
    GtkWidget *clock_area = gtk_alignment_new(0.5, 0.5, 1, 0);
    gtk_container_add(GTK_CONTAINER(clock_area), header);
    gtk_box_pack_start(GTK_BOX(root), clock_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), landscape ? gtk_vseparator_new() : gtk_hseparator_new(),
                       FALSE, FALSE, 0);

    // Panels: a grid in portrait (`columns`, `span`), one column in landscape.
    int cols = landscape ? 1 : g_cfg.columns, row = 0, col = 0;
    GtkWidget *table = gtk_table_new(1, cols, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 28);
    gtk_table_set_col_spacings(GTK_TABLE(table), 24);
    for (auto &p : g_cfg.panels) {
        int span = std::min(p.span, cols);
        if (col + span > cols) { row++; col = 0; }
        gtk_table_attach(GTK_TABLE(table), make_panel_widget(p),
                         col, col + span, row, row + 1,
                         GtkAttachOptions(GTK_FILL | GTK_EXPAND), GTK_FILL, 0, 0);
        col += span;
    }
    if (landscape) {
        // Panels take roughly the right 45% of the screen.
        GtkWidget *side = gtk_alignment_new(0, 0.5, 1, 0);
        gtk_container_add(GTK_CONTAINER(side), table);
        gtk_box_pack_start(GTK_BOX(root), side, TRUE, TRUE, 0);
    } else {
        gtk_box_pack_start(GTK_BOX(root), table, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(root);
    if (g_date) {
        // Repaint clock and date immediately in the new widgets.
        time_t now = time(nullptr);
        std::tm local = calendar_localtime(now, g_cfg.timezone);
        char buf[128];
        strftime(buf, sizeof buf, g_cfg.date_format.c_str(), &local);
        set_label(g_date, buf, g_cfg.title_size + 10, false);
    }
}

static gboolean on_configure(GtkWidget *, GdkEventConfigure *ev, gpointer) {
    build_layout(want_landscape(ev->width, ev->height));
    return FALSE;
}

static void toggle_orientation() {
    g_cfg.orientation = g_landscape == 1 ? "portrait" : "landscape";
    build_layout(g_landscape != 1);
    kindle_rotate(g_landscape == 1);
}

static void force_white(GtkWidget *w) {
    GdkColor white = {0, 0xffff, 0xffff, 0xffff}, black = {0, 0, 0, 0};
    gtk_widget_modify_bg(w, GTK_STATE_NORMAL, &white);
    gtk_widget_modify_fg(w, GTK_STATE_NORMAL, &black);
}

static gboolean on_key(GtkWidget *, GdkEventKey *ev, gpointer) {
    // GDK_Escape style names work on every GTK 2 release (Kindle ships 2.x).
    if (ev->keyval == GDK_Escape || ev->keyval == GDK_q) gtk_main_quit();
    if (ev->keyval == GDK_r)
        for (auto &p : g_cfg.panels) refresh_panel(&p);
    if (ev->keyval == GDK_o) toggle_orientation();
    return FALSE;
}

// Tap: refresh all panels. Hold for 2+ seconds: quit (the dashboard covers
// the whole screen, so this is the on-device way out).
static guint32 g_press_time = 0;

static gboolean on_press(GtkWidget *, GdkEventButton *ev, gpointer) {
    g_press_time = ev->time;
    return FALSE;
}

static gboolean on_release(GtkWidget *, GdkEventButton *ev, gpointer) {
    if (ev->time - g_press_time >= 2000) {
        fprintf(stderr, "desk-kindl: long press, quitting\n");
        gtk_main_quit();
        return FALSE;
    }
    for (auto &p : g_cfg.panels) refresh_panel(&p);
    full_refresh_cb(nullptr);
    return FALSE;
}

static std::string executable_path(const char *argv0) {
    char path[PATH_MAX + 1];
#if defined(__linux__)
    // Locate the running image without relying on argv[0] or rechecking its
    // executable permissions through GLib.
    ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1);
    if (length > 0 && length < (ssize_t)sizeof path - 1)
        return std::string(path, length);
#elif defined(__APPLE__)
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size) == 0) {
        char *resolved = realpath(path, nullptr);
        std::string result = resolved ? resolved : path;
        free(resolved);
        return result;
    }
#endif
    if (!argv0 || !*argv0) return "";
    // Do not require another executable-permission check for an explicit path:
    // the OS has already launched this program successfully.
    gchar *found = strchr(argv0, '/') ? g_strdup(argv0) : g_find_program_in_path(argv0);
    if (!found) return "";
    char *resolved = realpath(found, nullptr);
    std::string result = resolved ? resolved : found;
    free(resolved);
    g_free(found);
    return result;
}

static std::string find_config(int argc, char **argv, const std::string &binary) {
    if (argc > 1) return argv[1];
    const char *env = getenv("DESKKINDL_CONFIG");
    if (env) return env;
    gchar *dir = g_path_get_dirname(binary.c_str());
    std::string cand[] = {std::string(dir) + "/../config/dashboard.conf",
                          std::string(dir) + "/dashboard.conf",
                          "/mnt/us/extensions/deskkindl/config/dashboard.conf",
                          "config/dashboard.conf"};
    g_free(dir);
    for (auto &c : cand)
        if (g_file_test(c.c_str(), G_FILE_TEST_EXISTS)) return c;
    return cand[3];
}

int main(int argc, char **argv) {
    std::string binary = executable_path(argc > 0 ? argv[0] : nullptr);
    if (binary.empty()) {
        fprintf(stderr, "desk-kindl: cannot locate the running executable\n");
        return 1;
    }
    g_setenv("DESKKINDL_BIN", binary.c_str(), TRUE);
    gchar *binary_dir = g_path_get_dirname(binary.c_str());
    std::string zones = std::string(binary_dir) + "/../config/zoneinfo";
    if (!g_file_test((zones + "/zones.tab").c_str(), G_FILE_TEST_EXISTS))
        zones = std::string(binary_dir) + "/zoneinfo";
    calendar_zoneinfo(zones);
    g_free(binary_dir);
    if (argc == 4 && std::string(argv[1]) == "--calendar") {
        try {
            std::ifstream in(argv[2], std::ios::binary);
            if (!in) return 1;
            std::string data(5 * 1024 * 1024 + 1, '\0');
            in.read(&data[0], data.size());
            data.resize(in.gcount());
            std::string agenda = calendar_agenda(data, time(nullptr), argv[3]);
            fputs(agenda.c_str(), stdout);
            return 0;
        } catch (const std::exception &error) {
            fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }
    gtk_init(&argc, &argv);

    std::string cfg_path = find_config(argc, argv, binary);
    fprintf(stderr, "desk-kindl: config %s\n", cfg_path.c_str());
    if (!load_config(cfg_path, g_cfg)) {
        fprintf(stderr, "desk-kindl: cannot read config %s\n", cfg_path.c_str());
        return 1;
    }
    try {
        calendar_localtime(time(nullptr), g_cfg.timezone);
    } catch (const std::exception &error) {
        fprintf(stderr, "%s: %s\n", error.what(), g_cfg.timezone.c_str());
        return 1;
    }
    g_setenv("DESKKINDL_TIMEZONE", g_cfg.timezone.c_str(), TRUE);
    // Panel commands run relative to the config dir so scripts/ paths work.
    gchar *cfg_dir = g_path_get_dirname(cfg_path.c_str());
    if (chdir(cfg_dir) != 0) perror("chdir");
    g_free(cfg_dir);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    // Kindle's window manager places apps by title: L=layer A=application,
    // N=name, PC=N means no chrome (fullscreen).
    gtk_window_set_title(GTK_WINDOW(win),
                         g_cfg.kindle ? "L:A_N:application_ID:deskkindl_PC:N_O:URL"
                                      : "desk-kindl");
    GdkScreen *scr = gdk_screen_get_default();
    bool wide = g_cfg.orientation == "landscape";
    gtk_window_set_default_size(GTK_WINDOW(win),
                                g_cfg.kindle ? gdk_screen_get_width(scr) : (wide ? 1024 : 758),
                                g_cfg.kindle ? gdk_screen_get_height(scr) : (wide ? 758 : 1024));
    if (g_cfg.kindle) {
        // No gtk_window_fullscreen: the Kindle WM sizes the window from the
        // title hint (same setup as pomo-kndl, known to work on this device).
        fprintf(stderr, "desk-kindl: screen %dx%d, %d panels\n", gdk_screen_get_width(scr),
                gdk_screen_get_height(scr), (int)g_cfg.panels.size());
    }
    force_white(win);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), nullptr);

    GtkWidget *ebox = gtk_event_box_new();
    force_white(ebox);
    gtk_widget_add_events(ebox, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(ebox, "button-press-event", G_CALLBACK(on_press), nullptr);
    g_signal_connect(ebox, "button-release-event", G_CALLBACK(on_release), nullptr);
    gtk_container_add(GTK_CONTAINER(win), ebox);

    g_ebox = ebox;
    {
        int w = 0, h = 0;
        gtk_window_get_default_size(GTK_WINDOW(win), &w, &h);
        build_layout(g_cfg.kindle && g_cfg.orientation != "auto"
                         ? g_cfg.orientation == "landscape" : want_landscape(w, h));
    }
    g_signal_connect(win, "configure-event", G_CALLBACK(on_configure), nullptr);

    gtk_widget_show_all(win);

    tick_clock(nullptr);
    for (auto &p : g_cfg.panels) {
        refresh_panel(&p);
        g_timeout_add_seconds(p.interval, refresh_panel, &p);
    }
    if (g_cfg.kindle) kindle_setup();
    if (g_cfg.full_refresh > 0)
        g_timeout_add_seconds(g_cfg.full_refresh, full_refresh_cb, nullptr);

    fprintf(stderr, "desk-kindl: window shown, entering main loop\n");
    gtk_main();
    fprintf(stderr, "desk-kindl: main loop ended\n");
    if (g_cfg.kindle) kindle_teardown();
    return 0;
}
