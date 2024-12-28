/* welcomescreen */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "create-shm.h"
#include "async-timer.c"
#include <cairo/cairo.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-client-protocol.h"

/* It is important to make the following variables global to avoid memory leak */
int fd = 0;
static bool closed = false;
void *data = MAP_FAILED;
uint32_t fontSize = 0;
uint32_t fontSizeDup = 0;
char *windowListText = NULL;
cairo_t *cr = NULL;
cairo_surface_t *cr_surface = NULL;
struct wl_shm_pool *pool = NULL;
struct wl_buffer *shm_buffer = NULL;

/* main state struct */
struct client_state {
	/* Globals */
	struct wl_shm *wl_shm;
	struct wl_display *wl_display;
    struct xdg_wm_base *xdg_wm_base;
	struct wl_registry *wl_registry;
	struct wl_compositor *wl_compositor;
	/* Objects */
	struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
	/* State */
	int32_t width;
	int32_t height;
	int32_t stride;
	int32_t size;
};

/* Global listeners and callbacks for animated text */
struct wl_callback *cb = NULL;
static const struct wl_callback_listener wl_surface_frame_listener;

static void terminate_client() {
	closed = true;
}

/************************************************************************************************/
/************************************ DRAWING WELCOME SCREEN ************************************/
/************************************************************************************************/
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
	/* Sent by the compositor when it's no longer using this buffer */
	(void)data;
	wl_buffer_destroy(wl_buffer);
	wl_buffer = NULL;
}

static const struct wl_buffer_listener wl_buffer_listener = {
	.release = wl_buffer_release,
};

static struct wl_buffer *draw_frame(struct client_state *state) {
	(void)state;
	// Create shared memory file
	fd = allocate_shm_file(state->size);
	if (fd < 0) {
		perror("allocate_shm_file failed");
		return NULL;
	}

	data = mmap(NULL, state->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		perror("mmap failed");
		close(fd);
		return NULL;
	}

	memset(data, 0, state->size);
	// Create wl_shm_pool and wl_buffer
	pool = wl_shm_create_pool(state->wl_shm, fd, state->size);
	shm_buffer = wl_shm_pool_create_buffer(pool,
										   0,
										   state->width,
										   state->height,
										   state->stride,
										   WL_SHM_FORMAT_ARGB8888);

	// Create Cairo surface and context
	cr_surface = cairo_image_surface_create_for_data((unsigned char *)data,
													  CAIRO_FORMAT_ARGB32,
													  state->width,
													  state->height,
													  state->stride);
	cr = cairo_create(cr_surface);
	cairo_paint(cr);

	// Create a linear gradient pattern for the background
	cairo_pattern_t *gradient = cairo_pattern_create_linear(0, 0, 0, state->height);

	// Add color stops to define the gradient
	cairo_pattern_add_color_stop_rgba(gradient, 0.0, 0.0, 0.3, 0.0, 1.0); // Dark green at top
	cairo_pattern_add_color_stop_rgba(gradient, 1.0, 0.0, 0.2, 0.0, 1.0); // Darker green at bottom

	// Set the gradient as the source
	cairo_set_source(cr, gradient);

	// Fill the rectangle with the gradient
	cairo_rectangle(cr, 0, 0, state->width, state->height);
	cairo_fill(cr);

	// Destroy the gradient pattern to free memory
	cairo_pattern_destroy(gradient);

	// Drawing text
	fontSize = fontSize + 1;
	fontSizeDup = fontSizeDup + 1;
	if (fontSize == 140 && fontSizeDup < 420) {
		fontSize = 0;
	}
	if (fontSizeDup < 140) {
		windowListText = "Welcome";
	}
	else if (fontSizeDup > 140 && fontSizeDup < 280) {
		windowListText = "to";
	}
	else if (fontSizeDup > 280 && fontSizeDup < 420) {
		windowListText = "Woodland";
	}
	else if (fontSizeDup >= 420) {
		fontSize = 141;
	}
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, fontSize);

	// Measure text extents
	cairo_text_extents_t extents;
	cairo_text_extents(cr, windowListText, &extents);

	// Calculate centered position
	double x = (state->width / 2.0) - (extents.width / 2.0) - extents.x_bearing;
	double y = (state->height / 2.0) - (extents.height / 2.0) - extents.y_bearing;

	// Move to calculated position and draw the text
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, windowListText);

	cairo_surface_flush(cr_surface);
	cairo_surface_finish(cr_surface);
	cairo_close_path(cr);

	// Flush and cleanup Cairo resources
	cairo_destroy(cr);
	cairo_surface_destroy(cr_surface);

	// Cleanup shared memory
	wl_shm_pool_destroy(pool);
	munmap(data, state->size);
	close(fd);

	data = MAP_FAILED;
	cr_surface = NULL;
	cr = NULL;
	pool = NULL;
	fd = 0;

	wl_buffer_add_listener(shm_buffer, &wl_buffer_listener, state);
	return shm_buffer;
}

static void wl_surface_frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct client_state *state = data;

	wl_callback_destroy(cb);
	cb = wl_surface_frame(state->wl_surface);
	wl_callback_add_listener(cb, &wl_surface_frame_listener, state);

	/* Submit a frame for this event */
	struct wl_buffer *buffer = draw_frame(state);
	wl_surface_attach(state->wl_surface, buffer, 0, 0);
	wl_surface_damage_buffer(state->wl_surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(state->wl_surface);
}

static const struct wl_callback_listener wl_surface_frame_listener = {
	.done = wl_surface_frame_done,
};

/* Configuring XDG surface */
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct client_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    struct wl_buffer *buffer = draw_frame(state);
	wl_surface_attach(state->wl_surface, buffer, 0, 0);
	wl_surface_commit(state->wl_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	(void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *wl_registry, uint32_t name,
													const char *interface, uint32_t version) {

	(void)version;
	struct client_state *state = data;
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->wl_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
	}
	else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->wl_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
	}
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    }
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name) {
	(void)data;
	(void)name;
	(void)wl_registry;
	/* This space deliberately left blank */
	wl_registry_destroy(wl_registry);
}

static const struct wl_registry_listener wl_registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

int main(int argc, char *argv[]) {
	// Processing command line arguments
	uint32_t width = 0;
	uint32_t height = 0;

	if (argc <= 1) {
		width = 1920;
		height = 1080;
	}
	else {
		// User is supposed to use it like this: welcomescreen --resolution 1920x1080
		if (strcmp(argv[1], "--resolution") == 0) {
			if (argc > 2) {
				fprintf(stderr, "Resolution argument: %s\n", argv[2]);
				// Split the resolution argument "1920x1080" into width and height
				char *resolution = argv[2];
				char *delimiter = strchr(resolution, 'x');
				if (delimiter != NULL) {
					*delimiter = '\0'; // Split the string at 'x'
					width = (uint32_t)atoi(resolution); // First part is the width
					height = (uint32_t)atoi(delimiter + 1); // Second part is the height
				}
				else {
					fprintf(stderr, "Invalid resolution format. Use WIDTHxHEIGHT (e.g., 1920x1080).\n");
					return 1; // Exit with an error code
				}
			}
			else {
				fprintf(stderr, "Missing resolution value after --resolution.\n");
				return 1; // Exit with an error code
			}
		}
		else {
			fprintf(stderr, "Unknown argument: %s\n", argv[1]);
			return 1; // Exit with an error code
		}
	}

	struct client_state state = { 0 };
	state.width = width;
	state.height = height;
	state.stride = state.width * 4; // ARGB8888 requires width * 4
	state.size = state.stride * state.height;
    
	state.wl_display = wl_display_connect(NULL);
	cb = wl_display_sync(state.wl_display);
	state.wl_registry = wl_display_get_registry(state.wl_display);
	wl_registry_add_listener(state.wl_registry, &wl_registry_listener, &state);
	wl_display_roundtrip(state.wl_display);

	state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
	xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
	state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_app_id(state.xdg_toplevel, "welcomescreen");
    xdg_toplevel_set_title(state.xdg_toplevel, "welcomescreen");
	xdg_surface_set_window_geometry(state.xdg_surface, 0, 0, state.width, state.height);

	/// committing the surface showing the panel
	wl_surface_commit(state.wl_surface);

	struct wl_callback *cb = wl_surface_frame(state.wl_surface);
	wl_callback_add_listener(cb, &wl_surface_frame_listener, &state);

	// Ensure the Wayland connection is properly set up
	wl_display_roundtrip(state.wl_display);

	// Closing this client in 5 seconds
	async_timer(terminate_client, 8);

	while (!closed) {
		wl_display_dispatch(state.wl_display);
	}

	printf("Starting cleaning up\n");
	if (windowListText != NULL) {
		windowListText = NULL;
	}
	if (fd != 0) {
		close(fd);
		fd = 0;
	}
	if (data != MAP_FAILED) {
		munmap(data, state.size);
		data = MAP_FAILED;
	}
	if (cr_surface != NULL) {
		cairo_surface_destroy(cr_surface);
		cr_surface = NULL;
	}
	if (cr != NULL) {
		cairo_destroy(cr);
		cr = NULL;
	}
	if (pool != NULL) {
		wl_shm_pool_destroy(pool);
		pool = NULL;
	}
	if (shm_buffer != NULL) {
		wl_buffer_destroy(shm_buffer);
		shm_buffer = NULL;
	}
	if (state.xdg_toplevel) {
		xdg_toplevel_destroy(state.xdg_toplevel);
		state.xdg_toplevel = NULL;
	}
	if (state.xdg_surface) {
		xdg_surface_destroy(state.xdg_surface);
		state.xdg_surface = NULL;
	}
	if (state.xdg_wm_base) {
		xdg_wm_base_destroy(state.xdg_wm_base);
		state.xdg_wm_base = NULL;
	}
	if (state.wl_surface) {
		wl_surface_destroy(state.wl_surface);
		state.wl_surface = NULL;
	}
	if (state.wl_compositor) {
		wl_compositor_destroy(state.wl_compositor);
		state.wl_compositor = NULL;
	}
	if (state.wl_shm) {
		wl_shm_destroy(state.wl_shm);
		state.wl_shm = NULL;
	}
	if (state.wl_registry) {
		wl_registry_destroy(state.wl_registry);
		state.wl_registry = NULL;
	}
	if (state.wl_display) {
		wl_display_disconnect(state.wl_display);
		state.wl_display = NULL;
	}

	printf("Wayland client terminated!\n");

    return 0;
}
