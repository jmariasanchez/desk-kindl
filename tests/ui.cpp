// A real GTK layout check; run on a desktop with a display.
#define main dashboard_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <fcntl.h>
#include <sys/stat.h>

static bool panel_check_finished = false;

static void check_cached_panels() {
    for (auto &panel : g_cfg.panels) {
        const std::string raw = panel.text;
        const std::string displayed = gtk_label_get_text(GTK_LABEL(panel.body));
        GtkWidget *old = panel.body;
        g_object_ref(old);
        toggle_orientation();
        assert(panel.body != old);
        assert(panel.text == raw);
        assert(displayed == gtk_label_get_text(GTK_LABEL(panel.body)));
        g_object_unref(old);
    }
}

static void configure(GtkWidget *window, int width, int height) {
    GdkEventConfigure event = {};
    event.type = GDK_CONFIGURE;
    event.window = window->window;
    event.width = width;
    event.height = height;
    gboolean handled = FALSE;
    g_signal_emit_by_name(window, "configure-event", &event, &handled);
}

static void check_orientation(GtkWidget *window) {
    for (int i = 0; i < 12; ++i) {
        const bool was_landscape = g_landscape == 1;
        toggle_orientation();
        assert(g_landscape == !was_landscape);
        assert(g_cfg.orientation == (was_landscape ? "portrait" : "landscape"));
        configure(window, was_landscape ? 1024 : 600, was_landscape ? 758 : 800);
        assert(g_landscape == !was_landscape); // Explicit selection wins over shape.
    }
    check_cached_panels();
    g_cfg.orientation = "portrait";
    configure(window, 1024, 758);
    assert(g_landscape == 0);
    g_cfg.orientation = "landscape";
    configure(window, 600, 800);
    assert(g_landscape == 1);
    g_cfg.orientation = "auto";
    configure(window, 600, 800);
    assert(g_landscape == 0);
    GtkWidget *clock = g_clock;
    configure(window, 600, 800);
    assert(g_clock == clock); // An unchanged orientation must not rebuild.
    configure(window, 1024, 758);
    assert(g_landscape == 1);
    configure(window, 800, 800);
    assert(g_landscape == 0);
}

struct PendingPanel {
    std::string directory;
    std::string fifo;
    std::string command;
    int fd = -1;
    gint64 deadline = 0;
};

static gboolean finish_panel(gpointer data) {
    PendingPanel *pending = static_cast<PendingPanel *>(data);
    Panel &panel = g_cfg.panels.front();
    assert(g_get_monotonic_time() < pending->deadline);
    if (panel.pid || panel.text != "orientation-output\n") return TRUE;
    assert(std::string(gtk_label_get_text(GTK_LABEL(panel.body))) == "orientation-output");
    check_cached_panels();
    panel.command = pending->command;
    assert(close(pending->fd) == 0);
    assert(unlink(pending->fifo.c_str()) == 0);
    assert(rmdir(pending->directory.c_str()) == 0);
    delete pending;
    panel_check_finished = true;
    gtk_main_quit();
    return FALSE;
}

static void start_panel_check(GtkWidget *window) {
    for (const auto &panel : g_cfg.panels) assert(panel.pid == 0);
    check_orientation(window);
    if (g_getenv("DESKKINDL_TEST_IMMEDIATE")) {
        fprintf(stderr, "ui: probing GTK_RESIZE_IMMEDIATE\n");
        gtk_container_set_resize_mode(GTK_CONTAINER(window), GTK_RESIZE_IMMEDIATE);
        check_orientation(window);
    }
    PendingPanel *pending = new PendingPanel;
    gchar *directory = g_build_filename(g_get_tmp_dir(), "desk-kindl-ui-XXXXXX", nullptr);
    assert(g_mkdtemp(directory));
    pending->directory = directory;
    g_free(directory);
    pending->fifo = pending->directory + "/release";
    assert(mkfifo(pending->fifo.c_str(), 0600) == 0);
    pending->fd = open(pending->fifo.c_str(), O_RDWR | O_NONBLOCK);
    assert(pending->fd >= 0);
    // The FIFO gates completion on the test, not on a timing-sensitive sleep.
    Panel &panel = g_cfg.panels.front();
    pending->command = panel.command;
    gchar *quoted = g_shell_quote(pending->fifo.c_str());
    panel.command = std::string("read token < ") + quoted + "; printf 'orientation-output\\n'";
    g_free(quoted);
    refresh_panel(&panel);
    assert(panel.pid != 0);
    const GPid pid = panel.pid;
    const std::string cached = panel.text;
    GtkWidget *old = panel.body;
    g_object_ref(old);
    for (int i = 0; i < 8; ++i) {
        toggle_orientation();
        assert(panel.pid == pid);
        assert(panel.text == cached);
        assert(panel.body != old);
    }
    g_object_unref(old);
    assert(write(pending->fd, "go\n", 3) == 3);
    pending->deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    g_timeout_add(10, finish_panel, pending);
}

static gboolean check_layout(gpointer data) {
    int *stage = static_cast<int *>(data);
    GtkWidget *window = gtk_widget_get_toplevel(g_clock);
    int width, height;
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);
    static const int sizes[][2] = {{758, 1024}, {600, 800}, {1024, 758}};
    static const char *names[] = {"ui-large.png", "ui-small.png", "ui-landscape.png"};
    assert(width <= sizes[*stage][0]);
    assert(height <= sizes[*stage][1]);
    assert(g_landscape == (*stage == 2));
    PangoLayout *layout = gtk_label_get_layout(GTK_LABEL(g_clock));
    int text_width;
    pango_layout_get_pixel_size(layout, &text_width, nullptr);
    assert(text_width <= g_clock->allocation.width);
    assert(!pango_layout_is_ellipsized(layout));
    for (const auto &panel : g_cfg.panels) {
        int x, y;
        assert(gtk_widget_translate_coordinates(panel.body, window, 0, 0, &x, &y));
        assert(y + panel.body->allocation.height <= height);
        assert(x + panel.body->allocation.width <= width);
    }
    // Snapshot renders offscreen, so it works even where the X server
    // cannot read back the window contents.
    GdkPixmap *pixmap = gtk_widget_get_snapshot(window, nullptr);
    assert(pixmap);
    GdkPixbuf *shot = gdk_pixbuf_get_from_drawable(nullptr, pixmap,
        gdk_drawable_get_colormap(window->window), 0, 0, 0, 0, width, height);
    assert(shot);
    assert(gdk_pixbuf_save(shot, names[*stage], "png", nullptr, nullptr));
    g_object_unref(shot);
    g_object_unref(pixmap);
    if (++*stage < 3) {
        gtk_window_resize(GTK_WINDOW(window), sizes[*stage][0], sizes[*stage][1]);
        return TRUE;
    }
    start_panel_check(window);
    return FALSE;
}

int main(int argc, char **argv) {
    int stage = 0;
    g_timeout_add(1000, check_layout, &stage);
    int result = dashboard_main(argc, argv);
    assert(stage == 3);
    assert(panel_check_finished);
    return result;
}
