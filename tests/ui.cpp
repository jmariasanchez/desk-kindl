// A real GTK layout check; run on a desktop with a display.
#define main dashboard_main
#include "../src/main.cpp"
#undef main
#include <cassert>

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
    gtk_main_quit();
    return FALSE;
}

int main(int argc, char **argv) {
    int stage = 0;
    g_timeout_add(1000, check_layout, &stage);
    int result = dashboard_main(argc, argv);
    assert(stage == 3);
    return result;
}
