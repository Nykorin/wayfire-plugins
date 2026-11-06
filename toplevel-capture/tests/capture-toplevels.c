/*
 * capture-toplevels: captures every window once through
 * ext-foreign-toplevel-image-capture-source-v1 + ext-image-copy-capture-v1 and
 * writes OUTDIR/N-APPID.ppm, printing what the compositor sent.
 *
 * Usage: capture-toplevels OUTDIR [FRAMES]
 *   FRAMES  frames to capture per window (default 1); later ones wait for damage
 * Exits 0 if every window gave every frame.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

struct toplevel {
	struct ext_foreign_toplevel_handle_v1 *handle;
	char *app_id, *title;
	struct toplevel *next;
};

static struct wl_shm *shm;
static struct ext_foreign_toplevel_list_v1 *list;
static struct ext_foreign_toplevel_image_capture_source_manager_v1 *source_manager;
static struct ext_image_copy_capture_manager_v1 *copy_manager;
static struct toplevel *toplevels;

static void handle_title(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *t) {
	struct toplevel *tl = data; free(tl->title); tl->title = strdup(t);
}
static void handle_app_id(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *a) {
	struct toplevel *tl = data; free(tl->app_id); tl->app_id = strdup(a);
}
static void handle_noop(void *data, struct ext_foreign_toplevel_handle_v1 *h) {}
static void handle_identifier(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *i) {}
static const struct ext_foreign_toplevel_handle_v1_listener handle_listener = {
	.closed = handle_noop, .done = handle_noop, .title = handle_title,
	.app_id = handle_app_id, .identifier = handle_identifier,
};

static void list_toplevel(void *data, struct ext_foreign_toplevel_list_v1 *l,
		struct ext_foreign_toplevel_handle_v1 *handle) {
	struct toplevel *tl = calloc(1, sizeof(*tl));
	tl->handle = handle;
	tl->next = toplevels;
	toplevels = tl;
	ext_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, tl);
}
static void list_finished(void *data, struct ext_foreign_toplevel_list_v1 *l) {}
static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
	.toplevel = list_toplevel, .finished = list_finished,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
		list = wl_registry_bind(reg, name, &ext_foreign_toplevel_list_v1_interface, 1);
		ext_foreign_toplevel_list_v1_add_listener(list, &list_listener, NULL);
	} else if (strcmp(iface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0) {
		source_manager = wl_registry_bind(reg, name,
			&ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1);
	} else if (strcmp(iface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
		copy_manager = wl_registry_bind(reg, name, &ext_image_copy_capture_manager_v1_interface, 1);
	}
}
static void registry_remove(void *data, struct wl_registry *reg, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {
	.global = registry_global, .global_remove = registry_remove,
};

struct capture {
	uint32_t width, height, format;
	bool have_format, constraints_done, stopped;
	int frame_state; // 0 pending, 1 ready, 2 failed
	uint32_t fail_reason;
};

static void s_size(void *d, struct ext_image_copy_capture_session_v1 *s, uint32_t w, uint32_t h) {
	struct capture *c = d; c->width = w; c->height = h;
}
static void s_shm_format(void *d, struct ext_image_copy_capture_session_v1 *s, uint32_t f) {
	struct capture *c = d;
	// Prefer ARGB8888 (0) / XRGB8888 (1), the formats written out below
	if (!c->have_format || f == WL_SHM_FORMAT_ARGB8888) { c->format = f; c->have_format = true; }
}
static void s_dmabuf_device(void *d, struct ext_image_copy_capture_session_v1 *s, struct wl_array *a) {
	printf("    dmabuf_device advertised\n");
}
static void s_dmabuf_format(void *d, struct ext_image_copy_capture_session_v1 *s, uint32_t f, struct wl_array *m) {}
static void s_done(void *d, struct ext_image_copy_capture_session_v1 *s) {
	struct capture *c = d; c->constraints_done = true;
}
static void s_stopped(void *d, struct ext_image_copy_capture_session_v1 *s) {
	struct capture *c = d; c->stopped = true;
}
static const struct ext_image_copy_capture_session_v1_listener session_listener = {
	.buffer_size = s_size, .shm_format = s_shm_format, .dmabuf_device = s_dmabuf_device,
	.dmabuf_format = s_dmabuf_format, .done = s_done, .stopped = s_stopped,
};

static void f_transform(void *d, struct ext_image_copy_capture_frame_v1 *f, uint32_t t) {}
static void f_damage(void *d, struct ext_image_copy_capture_frame_v1 *f, int32_t x, int32_t y, int32_t w, int32_t h) {}
static void f_time(void *d, struct ext_image_copy_capture_frame_v1 *f, uint32_t a, uint32_t b, uint32_t c) {}
static void f_ready(void *d, struct ext_image_copy_capture_frame_v1 *f) {
	struct capture *c = d; c->frame_state = 1;
}
static void f_failed(void *d, struct ext_image_copy_capture_frame_v1 *f, uint32_t reason) {
	struct capture *c = d; c->frame_state = 2; c->fail_reason = reason;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
	.transform = f_transform, .damage = f_damage, .presentation_time = f_time,
	.ready = f_ready, .failed = f_failed,
};

// Dispatch until *flag is set or the timeout (ms) runs out
static bool wait_for(struct wl_display *dpy, bool (*cond)(struct capture *), struct capture *c, int timeout_ms) {
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (!cond(c)) {
		while (wl_display_prepare_read(dpy) != 0) {
			wl_display_dispatch_pending(dpy);
		}
		wl_display_flush(dpy);
		clock_gettime(CLOCK_MONOTONIC, &now);
		int left = timeout_ms - (int)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
		if (left <= 0) { wl_display_cancel_read(dpy); return false; }
		struct pollfd pfd = { .fd = wl_display_get_fd(dpy), .events = POLLIN };
		if (poll(&pfd, 1, left) <= 0) { wl_display_cancel_read(dpy); continue; }
		if (wl_display_read_events(dpy) < 0) return false;
		wl_display_dispatch_pending(dpy);
	}
	return true;
}
static bool constraints_ready(struct capture *c) { return c->constraints_done || c->stopped; }
static bool frame_done(struct capture *c) { return c->frame_state != 0 || c->stopped; }

static void write_ppm(const char *path, const uint8_t *px, uint32_t w, uint32_t h, uint32_t stride) {
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return; }
	fprintf(f, "P6\n%u %u\n255\n", w, h);
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			const uint8_t *p = px + y * stride + x * 4; // little-endian B G R A
			uint8_t rgb[3] = { p[2], p[1], p[0] };
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
}

static bool capture_one(struct wl_display *dpy, struct toplevel *tl, int index, const char *outdir, int frames) {
	struct capture c = {0};
	struct ext_image_capture_source_v1 *source =
		ext_foreign_toplevel_image_capture_source_manager_v1_create_source(source_manager, tl->handle);
	struct ext_image_copy_capture_session_v1 *session =
		ext_image_copy_capture_manager_v1_create_session(copy_manager, source, 0);
	ext_image_copy_capture_session_v1_add_listener(session, &session_listener, &c);

	bool ok = true;
	printf("[%d] %s \"%s\"\n", index, tl->app_id ? tl->app_id : "?", tl->title ? tl->title : "");
	if (!wait_for(dpy, constraints_ready, &c, 2000) || c.stopped || !c.have_format) {
		printf("    no constraints (stopped=%d format=%d)\n", c.stopped, c.have_format);
		ok = false;
		goto out;
	}
	printf("    constraints: %ux%u shm format %u\n", c.width, c.height, c.format);

	for (int n = 0; n < frames && ok; n++) {
		uint32_t stride = c.width * 4, size = stride * c.height;
		int fd = memfd_create("capture", MFD_CLOEXEC);
		if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); ok = false; break; }
		uint8_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
		struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, c.width, c.height, stride, c.format);
		wl_shm_pool_destroy(pool);
		close(fd);

		c.frame_state = 0;
		struct ext_image_copy_capture_frame_v1 *frame = ext_image_copy_capture_session_v1_create_frame(session);
		ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, &c);
		ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
		ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, c.width, c.height);
		ext_image_copy_capture_frame_v1_capture(frame);

		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		bool got = wait_for(dpy, frame_done, &c, 3000);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
		if (!got) {
			printf("    frame %d: timed out\n", n);
			ok = false;
		} else if (c.frame_state == 2) {
			printf("    frame %d: failed, reason %u, after %ld ms\n", n, c.fail_reason, ms);
			ok = false;
		} else if (c.stopped) {
			printf("    frame %d: session stopped\n", n);
			ok = false;
		} else {
			char path[4096];
			snprintf(path, sizeof(path), "%s/%d-%d-%s.ppm", outdir, index, n, tl->app_id ? tl->app_id : "unknown");
			write_ppm(path, px, c.width, c.height, stride);
			printf("    frame %d: ready after %ld ms -> %s\n", n, ms, path);
		}
		ext_image_copy_capture_frame_v1_destroy(frame);
		wl_buffer_destroy(buffer);
		munmap(px, size);
	}
out:
	ext_image_copy_capture_session_v1_destroy(session);
	ext_image_capture_source_v1_destroy(source);
	wl_display_roundtrip(dpy);
	return ok;
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: %s OUTDIR [FRAMES]\n", argv[0]); return 2; }
	int frames = argc > 2 ? atoi(argv[2]) : 1;
	struct wl_display *dpy = wl_display_connect(NULL);
	if (!dpy) { fprintf(stderr, "can't connect to Wayland\n"); return 1; }
	struct wl_registry *reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(dpy);
	wl_display_roundtrip(dpy);
	printf("globals: wl_shm=%s toplevel_list=%s toplevel_source_manager=%s copy_capture=%s\n",
		shm ? "yes" : "no", list ? "yes" : "no", source_manager ? "yes" : "no", copy_manager ? "yes" : "no");
	if (!shm || !list || !source_manager || !copy_manager) return 1;

	int index = 0, failed = 0;
	for (struct toplevel *tl = toplevels; tl; tl = tl->next) {
		if (!capture_one(dpy, tl, index++, argv[1], frames)) failed++;
	}
	printf("%d windows, %d failed\n", index, failed);
	return failed ? 1 : 0;
}
