// ext-workspace-bridge: Wayfire's workspace grids over ext_workspace_v1, for
// Noctalia's Workspace widget and other ext-workspace clients.
//
// Based on Wayfire's own ext-workspace plugin (plugins/protocols/ext-workspace.cpp,
// added in a352aa3b, PR #3092, not in a release as of 0.11), Copyright (c) 2018
// Iliya Bozhinov and Wayfire contributors, MIT License. Changes from it:
// - Workspaces are named 1, 2, 3, ... row by row, instead of
//   "<output>:workspace-X-Y"; clients like Noctalia label them by name.
// - Coordinates are (row, column), so clients that sort by them (Noctalia does)
//   order the grid the way Wayfire's own switching does: 1 2 3 / 4 5 6.
// - The capabilities passed to wlr_ext_workspace_handle_v1_create() are
//   capability flags (activate only), not the state flags ACTIVE | HIDDEN, which
//   advertised removal that isn't supported.
// - The protocol manager lives as long as the plugin, not as long as the last
//   per-output instance, so outputs going away and coming back don't create a
//   second ext_workspace_v1 global.
//
// Model: one workspace group per output, one workspace per cell of that
// output's workspace grid. Activate requests go through Wayfire's normal
// switching path (vswitch animations etc.).

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

#include <string>
#include <vector>

extern "C"
{
#include <wlr/types/wlr_ext_workspace_v1.h>
}

namespace wf
{
struct workspace_handle_data
{
    point_t grid_pos;
};

class ext_workspaces_intergration;

class wlr_ext_workspaces_manager : public custom_data_t
{
  public:
    wlr_ext_workspace_manager_v1 *manager;
    wf::wl_listener_wrapper on_commit;

    wlr_ext_workspaces_manager()
    {
        manager = wlr_ext_workspace_manager_v1_create(wf::get_core().display, 1);
        on_commit.set_callback([this] (void *data)
        {
            handle_commit(static_cast<wlr_ext_workspace_v1_commit_event*>(data));
        });
        on_commit.connect(&manager->events.commit);
    }

    void handle_commit(wlr_ext_workspace_v1_commit_event *event);
};

class ext_workspaces_intergration : public wf::per_output_plugin_instance_t
{
    wf::shared_data::ref_ptr_t<wlr_ext_workspaces_manager> manager;
    wlr_ext_workspace_group_handle_v1 *group = nullptr;
    // workspaces[row][column]
    std::vector<std::vector<wlr_ext_workspace_handle_v1*>> workspaces;

    wf::signal::connection_t<wf::workspace_changed_signal> on_workspace_changed =
        [this] (wf::workspace_changed_signal *ev)
    {
        set_active(ev->old_viewport, false);
        set_active(ev->new_viewport, true);
    };

    wf::signal::connection_t<wf::workspace_set_changed_signal> on_wset_changed =
        [this] (wf::workspace_set_changed_signal *ev)
    {
        on_grid_changed.disconnect();
        rebuild_workspaces();
        if (output->wset())
        {
            output->wset()->connect(&on_grid_changed);
        }
    };

    // Emitted only on the workspace set, not the output
    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_grid_changed =
        [this] (wf::workspace_grid_changed_signal *ev)
    {
        rebuild_workspaces();
    };

    void set_active(point_t pos, bool active)
    {
        if ((pos.y >= 0) && (pos.y < (int)workspaces.size()) &&
            (pos.x >= 0) && (pos.x < (int)workspaces[pos.y].size()))
        {
            wlr_ext_workspace_handle_v1_set_active(workspaces[pos.y][pos.x], active);
        }
    }

    void rebuild_workspaces()
    {
        teardown_workspaces();
        setup_workspaces();
    }

    void setup_workspaces()
    {
        auto wset = output->wset();
        if (!wset)
        {
            return;
        }

        dimensions_t ws_dim = wset->get_workspace_grid_size();
        workspaces.resize(ws_dim.height,
            std::vector<wlr_ext_workspace_handle_v1*>(ws_dim.width, nullptr));

        for (int i = 0; i < ws_dim.height; i++)
        {
            for (int j = 0; j < ws_dim.width; j++)
            {
                // IDs must be unique across the whole manager, so they include
                // the output
                std::string id =
                    output->to_string() + ":workspace-" +
                    std::to_string(j) + "-" + std::to_string(i);
                // Shown by clients: 1-based, row by row
                std::string name = std::to_string(i * ws_dim.width + j + 1);

                workspaces[i][j] = wlr_ext_workspace_handle_v1_create(manager->manager,
                    id.c_str(), EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
                wlr_ext_workspace_handle_v1_set_group(workspaces[i][j], group);
                wlr_ext_workspace_handle_v1_set_name(workspaces[i][j], name.c_str());

                // (row, column): clients sorting by coordinates get 1 2 3 / 4 5 6
                uint32_t coords[2] = {(uint32_t)i, (uint32_t)j};
                wlr_ext_workspace_handle_v1_set_coordinates(
                    workspaces[i][j], coords, 2);

                auto *ws_data = new workspace_handle_data();
                ws_data->grid_pos = {j, i};
                workspaces[i][j]->data = ws_data;
            }
        }

        set_active(wset->get_current_workspace(), true);
    }

    void teardown_workspaces()
    {
        for (auto& row : workspaces)
        {
            for (auto *ws : row)
            {
                delete static_cast<workspace_handle_data*>(ws->data);
                wlr_ext_workspace_handle_v1_destroy(ws);
            }
        }

        workspaces.clear();
    }

  public:
    void handle_activate(point_t grid_pos)
    {
        if (auto wset = output->wset())
        {
            wset->request_workspace(grid_pos);
        }
    }

    void init() override
    {
        group = wlr_ext_workspace_group_handle_v1_create(manager->manager, 0);
        group->data = this;

        wlr_ext_workspace_group_handle_v1_output_enter(group, output->handle);

        setup_workspaces();

        output->connect(&on_workspace_changed);
        output->connect(&on_wset_changed);
        if (output->wset())
        {
            output->wset()->connect(&on_grid_changed);
        }
    }

    void fini() override
    {
        if (group)
        {
            group->data = nullptr;
        }

        teardown_workspaces();

        if (group)
        {
            wlr_ext_workspace_group_handle_v1_destroy(group);
            group = nullptr;
        }
    }
};

void wlr_ext_workspaces_manager::handle_commit(wlr_ext_workspace_v1_commit_event *event)
{
    wlr_ext_workspace_v1_request *req, *tmp;
    wl_list_for_each_safe(req, tmp, event->requests, link)
    {
        if (req->type != WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE)
        {
            // Creating, removing and assigning workspaces aren't advertised
            continue;
        }

        auto *ws = req->activate.workspace;
        if (!ws || !ws->group)
        {
            continue;
        }

        auto *ws_data     = static_cast<workspace_handle_data*>(ws->data);
        auto *integration = static_cast<ext_workspaces_intergration*>(ws->group->data);
        if (ws_data && integration)
        {
            integration->handle_activate(ws_data->grid_pos);
        }
    }
}

class ext_workspaces_plugin_t : public wf::per_output_plugin_t<wf::ext_workspaces_intergration>
{
    // Holds the manager for the plugin's whole life: with only the per-output
    // instances holding it, losing every output would free it, and the next
    // output would create a second ext_workspace_v1 global (wlroots can't
    // destroy the first one)
    wf::shared_data::ref_ptr_t<wlr_ext_workspaces_manager> manager;

  public:
    bool is_unloadable() override
    {
        return false;
    }
};
} // namespace wf

DECLARE_WAYFIRE_PLUGIN(wf::ext_workspaces_plugin_t);
