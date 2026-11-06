// ext-workspace-bridge: expose Wayfire's workspace grids via ext_workspace_v1.
//
// Model: one ext-workspace *group* per output, one ext-workspace *workspace*
// per cell of that output's workspace grid.
//
// Every signal handler just calls refresh(), which reconciles protocol state
// against Wayfire's real state, so a signal firing at an unexpected time can't
// leave it out of step.
//
// Built against Wayfire 0.11 / wlroots 0.20. Wayfire emits
// workspace_changed_signal on both the output and its workspace set, but
// workspace_grid_changed_signal only on the workspace set, so that one is
// followed through the output's current set.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/util.hpp>
#include <wayfire/workspace-set.hpp>

extern "C"
{
#include <wayland-server-core.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
}

class ext_workspace_bridge_t : public wf::plugin_interface_t
{
  struct cell_t
  {
    wlr_ext_workspace_handle_v1 *handle = nullptr;
    wf::point_t pos{0, 0};
  };

  struct out_state_t
  {
    wf::output_t *output = nullptr;
    wlr_ext_workspace_group_handle_v1 *group = nullptr;
    wf::dimensions_t grid{0, 0};
    std::vector<std::unique_ptr<cell_t>> cells;

    // Per-output signal connections.
    wf::signal::connection_t<wf::workspace_changed_signal> on_ws_changed;
    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_grid_changed;
    wf::signal::connection_t<wf::workspace_set_changed_signal> on_wset_changed;
  };

    wlr_ext_workspace_manager_v1 *manager = nullptr;
    std::vector<std::unique_ptr<out_state_t>> outputs;
    wf::wl_listener_wrapper on_commit;

    wf::signal::connection_t<wf::output_added_signal> on_output_added = [this] (wf::output_added_signal *ev)
    {
        add_output(ev->output);
    };

    wf::signal::connection_t<wf::output_removed_signal> on_output_removed = [this] (wf::output_removed_signal *ev)
    {
        remove_output(ev->output);
    };

  public:
    void init() override
    {
        manager = wlr_ext_workspace_manager_v1_create(wf::get_core().display, 1);
        if (!manager)
        {
            LOGE("ext-workspace-bridge: failed to create ext_workspace_manager_v1");
            return;
        }

        // Client requests (activate, ...) arrive batched via the manager's
        // commit event in wlroots 0.19+.
        on_commit.set_callback([this] (void *data)
        {
            handle_commit((wlr_ext_workspace_v1_commit_event*)data);
        });
        on_commit.connect(&manager->events.commit);

        wf::get_core().output_layout->connect(&on_output_added);
        wf::get_core().output_layout->connect(&on_output_removed);

        for (auto *o : wf::get_core().output_layout->get_outputs())
        {
            add_output(o);
        }

        LOGI("ext-workspace-bridge: ext_workspace_v1 global registered");
    }

    void fini() override
    {
        on_output_added.disconnect();
        on_output_removed.disconnect();
        on_commit.disconnect();
        while (!outputs.empty())
        {
            teardown(*outputs.back());
            outputs.pop_back();
        }
        // The manager global is owned by the wl_display and destroyed with it.
    }

    bool is_unloadable() override
    {
        // The global can't be cleanly unregistered while clients are bound.
        return false;
    }

  private:
    void handle_commit(wlr_ext_workspace_v1_commit_event *ev)
    {
        for (wl_list *l = ev->requests->next; l != ev->requests; l = l->next)
        {
            auto *req = reinterpret_cast<wlr_ext_workspace_v1_request*>(
                (char*)l - offsetof(wlr_ext_workspace_v1_request, link));
            if ((req->type != WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE) ||
                !req->activate.workspace)
            {
                continue;
            }

            for (auto& st : outputs)
            {
                for (auto& c : st->cells)
                {
                    if (c->handle == req->activate.workspace)
                    {
                        // Goes through Wayfire's normal switching path
                        // (vswitch animations etc.).
                        st->output->wset()->request_workspace(c->pos);
                    }
                }
            }
        }
    }

    void add_output(wf::output_t *o)
    {
        if (!manager || !o || find(o))
        {
            return;
        }

        auto st = std::make_unique<out_state_t>();
        st->output = o;

        // No group-level capabilities (creating or assigning workspaces)
        st->group = wlr_ext_workspace_group_handle_v1_create(manager, 0);
        wlr_ext_workspace_group_handle_v1_output_enter(st->group, o->handle);

        auto *raw = st.get();
        st->on_ws_changed = [this, raw] (wf::workspace_changed_signal*) { refresh(*raw); };
        st->on_grid_changed = [this, raw] (wf::workspace_grid_changed_signal*) { refresh(*raw); };
        // A new workspace set (e.g. from the wsets plugin) brings its own grid
        // and current workspace
        st->on_wset_changed = [this, raw] (wf::workspace_set_changed_signal*)
        {
            follow_wset(*raw);
            refresh(*raw);
        };

        o->connect(&st->on_ws_changed);
        o->connect(&st->on_wset_changed);
        follow_wset(*st);

        outputs.push_back(std::move(st));
        refresh(*outputs.back());
    }

    // The grid signal is only emitted on the workspace set
    void follow_wset(out_state_t& st)
    {
        st.on_grid_changed.disconnect();
        if (auto wset = st.output->wset())
        {
            wset->connect(&st.on_grid_changed);
        }
    }

    void remove_output(wf::output_t *o)
    {
        for (auto it = outputs.begin(); it != outputs.end(); ++it)
        {
            if ((*it)->output == o)
            {
                teardown(**it);
                outputs.erase(it);
                return;
            }
        }
    }

    out_state_t *find(wf::output_t *o)
    {
        for (auto& st : outputs)
        {
            if (st->output == o)
            {
                return st.get();
            }
        }

        return nullptr;
    }

    void destroy_cells(out_state_t& st)
    {
        for (auto& c : st.cells)
        {
            wlr_ext_workspace_handle_v1_destroy(c->handle);
        }

        st.cells.clear();
    }

    void teardown(out_state_t& st)
    {
        st.on_ws_changed.disconnect();
        st.on_grid_changed.disconnect();
        st.on_wset_changed.disconnect();
        destroy_cells(st);
        if (st.group)
        {
            wlr_ext_workspace_group_handle_v1_destroy(st.group);
            st.group = nullptr;
        }
    }

    // Recreate the workspace handles for this output's grid.
    void rebuild(out_state_t& st, wf::dimensions_t grid)
    {
        destroy_cells(st);
        st.grid = grid;

        const std::string out_name = st.output->to_string();

        for (int y = 0; y < grid.height; y++)
        {
            for (int x = 0; x < grid.width; x++)
            {
                auto cell = std::make_unique<cell_t>();
                cell->pos = {x, y};

                // IDs must be unique across the whole manager, so include the output.
                const std::string id = out_name + ":" + std::to_string(x) + "," + std::to_string(y);
                // Human-visible name: 1-based, row-major (matches how most
                // widgets number a flat list of workspaces).
                const std::string name = std::to_string(y * grid.width + x + 1);

                // Clients can only activate workspaces, not remove or move them
                cell->handle = wlr_ext_workspace_handle_v1_create(manager, id.c_str(),
                    EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
                wlr_ext_workspace_handle_v1_set_group(cell->handle, st.group);
                wlr_ext_workspace_handle_v1_set_name(cell->handle, name.c_str());

                // Row-major: coordinates[0] = row, coordinates[1] = column. Clients such
                // as Noctalia sort workspaces lexicographically by this array, so this
                // yields 1,2,3 / 4,5,6 order for a 3x2 grid instead of 1,4,2,5,3,6.
                const uint32_t coords[2] = {(uint32_t)y, (uint32_t)x};
                wlr_ext_workspace_handle_v1_set_coordinates(cell->handle, coords, 2);

                st.cells.push_back(std::move(cell));
            }
        }
    }

    // Reconcile protocol state with Wayfire's state for one output.
    void refresh(out_state_t& st)
    {
        auto wset = st.output->wset();
        if (!wset)
        {
            return;
        }

        const auto grid = wset->get_workspace_grid_size();
        const auto cur  = wset->get_current_workspace();

        if ((grid.width != st.grid.width) || (grid.height != st.grid.height) || st.cells.empty())
        {
            rebuild(st, grid);
        }

        for (auto& c : st.cells)
        {
            wlr_ext_workspace_handle_v1_set_active(c->handle, c->pos == cur);
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(ext_workspace_bridge_t);
