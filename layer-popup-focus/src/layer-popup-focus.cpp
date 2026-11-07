// layer-popup-focus: give keyboard focus to grabbing popups of layer surfaces that
// don't take keyboard input, for the clients listed in the "clients" option.
//
// PROTOTYPE: built against Wayfire 0.11 / wlroots 0.20, but never run.
//
// Since Wayfire 0.11 (commit f7f5461b, for wayfire#3025) a popup of a layer surface
// with keyboard_interactivity none gets no keyboard focus, as the layer-shell protocol
// asks, even when it grabs input. lxqt-panel's menus are such popups, so Esc, the
// arrow keys and typing in Fancy Menu's search never reach them (lxqt-panel#2504).
//
// wayfire_xdg_popup::map() emits view_mapped_signal before its own focus check, so
// the plugin sees each popup just before core declines it. For a grabbing popup whose
// topmost parent is a desktop-environment view that won't take keyboard focus, owned
// by a listed client, it enters the popup's surface on the wlr_seat directly, the way
// core enters popups it does focus (wlr_seat_keyboard_enter, past the grab). While the
// grab lasts, wlroots' popup keyboard grab ignores Wayfire's own keyboard enters.
//
// Wayfire's focus never moves to the popup, so nothing else hands keyboard focus back
// when the popup goes away: at pre-unmap the plugin enters the parent popup (for a
// submenu) or the view Wayfire considers focused, if the popup still had the focus.

#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <wayfire/core.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/view.hpp>

extern "C"
{
#include <wayland-server-core.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
}

class layer_popup_focus_t : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_view_pre_unmap);
        wf::get_core().connect(&on_view_unmapped);
    }

    void fini() override
    {
        on_view_mapped.disconnect();
        on_view_pre_unmap.disconnect();
        on_view_unmapped.disconnect();
        focused.clear();
    }

  private:
    // Space-separated process names (/proc/PID/comm); empty means every client
    wf::option_wrapper_t<std::string> clients{"layer-popup-focus/clients"};

    // Popups this plugin focused, with the surface to hand focus back to: the parent
    // popup for a submenu, nullptr for "whatever Wayfire has focused"
    std::map<wf::view_interface_t*, wlr_surface*> focused;

    bool client_listed(wl_client *client)
    {
        std::istringstream names{(std::string)clients};
        std::string name;
        if (!(names >> name))
        {
            return true;
        }

        pid_t pid = 0;
        wl_client_get_credentials(client, &pid, nullptr, nullptr);
        std::string comm;
        std::ifstream{"/proc/" + std::to_string(pid) + "/comm"} >> comm;
        do
        {
            if (name == comm)
            {
                return true;
            }
        } while (names >> name);

        return false;
    }

    static void enter(wlr_surface *surface)
    {
        auto seat = wf::get_core().seat.get();
        auto keys = seat->get_pressed_keys();
        auto kbd  = wlr_seat_get_keyboard(seat->seat);
        wlr_seat_keyboard_enter(seat->seat, surface, keys.data(), keys.size(),
            kbd ? &kbd->modifiers : nullptr);
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [this] (wf::view_mapped_signal *ev)
    {
        auto surface = ev->view->get_wlr_surface();
        auto popup   = surface ? wlr_xdg_popup_try_from_wlr_surface(surface) : nullptr;
        if (!popup || !popup->seat || !popup->parent)
        {
            return;
        }

        // Walk up to the popup tree's root, remembering the direct parent popup
        wlr_surface *parent_popup = wlr_xdg_popup_try_from_wlr_surface(popup->parent) ?
            popup->parent : nullptr;
        wlr_surface *root = popup->parent;
        while (auto p = wlr_xdg_popup_try_from_wlr_surface(root))
        {
            if (!p->parent)
            {
                return;
            }

            root = p->parent;
        }

        auto root_view = wf::wl_surface_to_wayfire_view(root->resource);
        if (!root_view || (root_view->role != wf::VIEW_ROLE_DESKTOP_ENVIRONMENT) ||
            root_view->get_keyboard_focus_surface() || !ev->view->get_client() ||
            !client_listed(ev->view->get_client()))
        {
            return;
        }

        LOGD("layer-popup-focus: focusing popup ", ev->view.get());
        enter(surface);
        focused[ev->view.get()] = parent_popup;
    };

    wf::signal::connection_t<wf::view_pre_unmap_signal> on_view_pre_unmap =
        [this] (wf::view_pre_unmap_signal *ev)
    {
        auto it = focused.find(ev->view.get());
        if (it == focused.end())
        {
            return;
        }

        wlr_surface *restore = it->second;
        focused.erase(it);

        // Leave the focus alone if something else has taken it since
        auto seat = wf::get_core().seat->seat;
        if (seat->keyboard_state.focused_surface != ev->view->get_wlr_surface())
        {
            return;
        }

        if (!restore)
        {
            auto active = wf::get_core().seat->get_active_view();
            restore = active ? active->get_keyboard_focus_surface() : nullptr;
        }

        if (restore)
        {
            enter(restore);
        } else
        {
            wlr_seat_keyboard_clear_focus(seat);
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [this] (wf::view_unmapped_signal *ev)
    {
        // In case a popup unmapped without pre-unmap
        focused.erase(ev->view.get());
    };
};

DECLARE_WAYFIRE_PLUGIN(layer_popup_focus_t);
