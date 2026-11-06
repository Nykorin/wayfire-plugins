/*
 * toplevel-capture: ext-foreign-toplevel-image-capture-source-v1 for Wayfire.
 *
 * Lets clients that capture through ext-image-copy-capture-v1 (Noctalia's window
 * switcher previews, xdg-desktop-portal-wlr window sharing, ...) capture a single
 * window. Wayfire 0.11 creates the copy-capture manager and the per-output source
 * manager but not this one.
 *
 * The toplevel handles come from Wayfire's ext-toplevel plugin, which stores the
 * view in handle->data, so that plugin must be enabled too.
 *
 * A source renders the view's surface tree (the window and its server-side
 * decoration, without transformers such as wobbly or cube) into an off-screen
 * buffer, the same way view_interface_t::take_snapshot() does. While at least one
 * capture session runs it keeps render instances of that tree alive: their damage
 * tells it when the window changed, and being visible keeps the client's frame
 * callbacks coming even when the window is on another workspace.
 */

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/render.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>
#include <wayfire/view.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include <time.h>

extern "C"
{
#define static
#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#undef static
}

namespace
{
class view_capture_source_t;

// wlroots calls the source back through this table; `base` is the first member of
// view_capture_source_t, so the cast below recovers the object.
void source_start(wlr_ext_image_capture_source_v1 *base, bool with_cursors);
void source_stop(wlr_ext_image_capture_source_v1 *base);
void source_request_frame(wlr_ext_image_capture_source_v1 *base, bool schedule_frame);
void source_copy_frame(wlr_ext_image_capture_source_v1 *base,
    wlr_ext_image_copy_capture_frame_v1 *frame,
    wlr_ext_image_capture_source_v1_frame_event *event);

const wlr_ext_image_capture_source_v1_interface source_impl = {
    .start = source_start,
    .stop  = source_stop,
    .request_frame = source_request_frame,
    .copy_frame    = source_copy_frame,
    .get_pointer_cursor = nullptr,
};

class view_capture_source_t
{
  public:
    // Must stay the first member (see source_impl)
    wlr_ext_image_capture_source_v1 base;

    explicit view_capture_source_t(wayfire_view view) : view(view)
    {
        wlr_ext_image_capture_source_v1_init(&base, &source_impl);
        update_constraints(current_buffer_size(), false);
        render_idle.set_callback([this] { render_and_emit(); });
    }

    ~view_capture_source_t()
    {
        render_idle.disconnect();
        instances.reset();
        // Emits destroy: wlroots ends every session on this source and makes its
        // resources inert
        wlr_ext_image_capture_source_v1_finish(&base);
    }

    view_capture_source_t(const view_capture_source_t&) = delete;
    view_capture_source_t& operator =(const view_capture_source_t&) = delete;

    void start()
    {
        if (++sessions > 1)
        {
            return;
        }

        damaged = true;
        create_instances();
    }

    void stop()
    {
        if (sessions == 0 || --sessions > 0)
        {
            return;
        }

        render_idle.disconnect();
        frame_requested = false;
        instances.reset();
        snapshot.free();
    }

    void request_frame(bool schedule_frame)
    {
        frame_requested = true;
        if (schedule_frame)
        {
            // The session still has damage the client hasn't received
            damaged = true;
        }

        maybe_schedule_render();
    }

    void copy_frame(wlr_ext_image_copy_capture_frame_v1 *frame)
    {
        wlr_buffer *buffer = snapshot.get_buffer();
        if (!buffer)
        {
            wlr_ext_image_copy_capture_frame_v1_fail(frame,
                EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
            return;
        }

        // On failure this sends the reason (e.g. buffer_constraints after a resize)
        // and destroys the frame itself
        if (wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, buffer, wf::get_core().renderer))
        {
            wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &rendered_at);
        }
    }

  private:
    wayfire_view view;
    size_t sessions = 0;
    bool damaged    = true;
    bool frame_requested = false;
    wf::auxilliary_buffer_t snapshot;
    timespec rendered_at{};
    std::unique_ptr<wf::scene::render_instance_manager_t> instances;
    wf::wl_idle_call render_idle;

    float output_scale() const
    {
        auto output = view->get_output();
        return output ? output->handle->scale : 1.0f;
    }

    // The size auxilliary_buffer_t::allocate() gives for the surface tree
    wf::dimensions_t current_buffer_size() const
    {
        auto bbox  = view->get_surface_root_node()->get_bounding_box();
        float scale = output_scale();
        return {
            std::max(1, (int)std::ceil(bbox.width * scale)),
            std::max(1, (int)std::ceil(bbox.height * scale)),
        };
    }

    void create_instances()
    {
        auto on_damage = [this] (const wf::regionf_t&)
        {
            damaged = true;
            maybe_schedule_render();
        };

        std::vector<wf::scene::node_ptr> nodes{view->get_surface_root_node()};
        instances = std::make_unique<wf::scene::render_instance_manager_t>(nodes, on_damage,
            view->get_output());

        // Count the whole tree as visible wherever the window is, so its surfaces
        // keep getting frame callbacks while it's being captured
        const double big = 1e5;
        instances->set_visibility_region(wf::regionf_t{wf::geometry_t{-big, -big, 2 * big, 2 * big}});
    }

    void maybe_schedule_render()
    {
        if (frame_requested && damaged && (sessions > 0) && !render_idle.is_connected())
        {
            // Coalesce damage from one event loop iteration into a single render
            render_idle.run_once();
        }
    }

    // Sizes and formats the client may allocate buffers with: shm buffers in
    // formats the renderer can read pixels back into.
    void update_constraints(wf::dimensions_t size, bool notify)
    {
        base.width  = size.width;
        base.height = size.height;

        if (!base.shm_formats)
        {
            static const uint32_t shm[] = {DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888};
            base.shm_formats = (uint32_t*)calloc(2, sizeof(uint32_t));
            if (base.shm_formats)
            {
                std::copy(std::begin(shm), std::end(shm), base.shm_formats);
                base.shm_formats_len = 2;
            }
        }

        // No dmabuf formats: the renderer's render formats, which a dmabuf frame
        // would need, aren't public API in wlroots 0.20, and advertising ones it
        // can't render into would fail every frame. Clients fall back to shm.

        if (notify)
        {
            wl_signal_emit_mutable(&base.events.constraints_update, nullptr);
        }
    }

    bool render_snapshot()
    {
        auto output = view->get_output();
        if (!output || !instances)
        {
            return false;
        }

        auto root = view->get_surface_root_node();
        wf::geometry_t bbox = root->get_bounding_box();
        if ((bbox.width <= 0) || (bbox.height <= 0))
        {
            return false;
        }

        float scale = output->handle->scale;
        // 8-bit, not take_snapshot()'s linear FP16 on HDR outputs: clients ask for
        // ARGB8888/XRGB8888
        if (snapshot.allocate(wf::dimensions(bbox), scale) ==
            wf::buffer_reallocation_result_t::FAILED)
        {
            return false;
        }

        wf::render_target_t target{snapshot};
        target.geometry = bbox;
        target.scale    = scale;

        wf::render_pass_params_t params;
        params.background_color = {0, 0, 0, 0};
        params.damage    = bbox;
        params.target    = target;
        params.instances = &instances->get_instances();
        params.flags     = wf::RPASS_CLEAR_BACKGROUND;
        wf::render_pass_t::run(params);

        clock_gettime(CLOCK_MONOTONIC, &rendered_at);
        return true;
    }

    void render_and_emit()
    {
        if (!frame_requested || (sessions == 0) || !render_snapshot())
        {
            return;
        }

        damaged = false;
        frame_requested = false;

        auto size = snapshot.get_size();
        if (((uint32_t)size.width != base.width) || ((uint32_t)size.height != base.height))
        {
            // Sessions resend the constraints; the client's pending frame then fails
            // with buffer_constraints and it reallocates
            update_constraints(size, true);
        }

        pixman_region32_t damage;
        pixman_region32_init_rect(&damage, 0, 0, size.width, size.height);
        wlr_ext_image_capture_source_v1_frame_event event{.damage = &damage};
        // Sessions with a capturing frame call copy_frame() from inside this
        wl_signal_emit_mutable(&base.events.frame, &event);
        pixman_region32_fini(&damage);
    }
};

view_capture_source_t *from_base(wlr_ext_image_capture_source_v1 *base)
{
    return reinterpret_cast<view_capture_source_t*>(base);
}

void source_start(wlr_ext_image_capture_source_v1 *base, bool)
{
    from_base(base)->start();
}

void source_stop(wlr_ext_image_capture_source_v1 *base)
{
    from_base(base)->stop();
}

void source_request_frame(wlr_ext_image_capture_source_v1 *base, bool schedule_frame)
{
    from_base(base)->request_frame(schedule_frame);
}

void source_copy_frame(wlr_ext_image_capture_source_v1 *base,
    wlr_ext_image_copy_capture_frame_v1 *frame, wlr_ext_image_capture_source_v1_frame_event*)
{
    from_base(base)->copy_frame(frame);
}
}

class wayfire_toplevel_capture_t : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        manager = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(
            wf::get_core().display, 1);
        if (!manager)
        {
            LOGE("toplevel-capture: failed to create the toplevel image capture source manager");
            return;
        }

        on_new_request.set_callback([this] (void *data)
        {
            handle_request(
                (wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request*)data);
        });
        on_new_request.connect(&manager->events.new_request);
        on_manager_destroy.set_callback([this] (void*)
        {
            on_new_request.disconnect();
            on_manager_destroy.disconnect();
            manager = nullptr;
        });
        on_manager_destroy.connect(&manager->events.destroy);

        wf::get_core().connect(&on_view_unmapped);
    }

    void fini() override
    {
        sources.clear();
        on_new_request.disconnect();
        on_manager_destroy.disconnect();
        // wlroots has no destroy function for the manager; the global stays (with no
        // listener, so new sources come out inert), which is why this plugin isn't
        // unloadable
    }

    bool is_unloadable() override
    {
        return false;
    }

  private:
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *manager = nullptr;
    wf::wl_listener_wrapper on_new_request;
    wf::wl_listener_wrapper on_manager_destroy;
    // One source per view, shared by every client and session capturing it
    std::map<wf::view_interface_t*, std::unique_ptr<view_capture_source_t>> sources;

    static wayfire_view find_view(wlr_ext_foreign_toplevel_handle_v1 *handle)
    {
        // ext-toplevel sets handle->data to the view; check it's still a live,
        // mapped one rather than trusting the pointer
        auto ptr = static_cast<wf::view_interface_t*>(handle->data);
        if (!ptr)
        {
            return nullptr;
        }

        for (auto& view : wf::get_core().get_all_views())
        {
            if ((view.get() == ptr) && view->is_mapped())
            {
                return view;
            }
        }

        return nullptr;
    }

    void handle_request(wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request)
    {
        wayfire_view view = request->toplevel_handle ? find_view(request->toplevel_handle) : nullptr;
        if (!view)
        {
            // An inert source: the client's session is stopped right away
            wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, nullptr);
            return;
        }

        auto& source = sources[view.get()];
        if (!source)
        {
            source = std::make_unique<view_capture_source_t>(view);
        }

        wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, &source->base);
    }

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [this] (wf::view_unmapped_signal *ev)
    {
        // ext-toplevel drops the handle at unmap too, so capture ends with the window
        sources.erase(ev->view.get());
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_toplevel_capture_t);
