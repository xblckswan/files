/*
 * Copyright (C) 2023 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib/gi18n.h>

#include <eel/eel-vfs-extensions.h>

#include "nautilus-rename-file-popover.h"

#include "nautilus-directory.h"
#include "nautilus-file-private.h"
#include "nautilus-filename-message.h"


#define RENAME_ENTRY_MIN_CHARS 30
#define RENAME_ENTRY_MAX_CHARS 50

struct _NautilusRenameFilePopover
{
    GtkPopover parent_instance;

    NautilusFile *target_file;
    char *target_file_name;
    gboolean target_is_folder;
    NautilusDirectory *containing_directory;
    gboolean should_rename;
    NautilusRenameCallback callback;

    GtkLabel *error_label;
    GtkRevealer *error_revealer;
    GtkWidget *activate_button;
    GtkWidget *name_entry;
    GtkWidget *title_label;

    guint delayed_message_timer_id;
    gulong file_changed_handler_id;
};

G_DEFINE_TYPE (NautilusRenameFilePopover,
               nautilus_rename_file_popover,
               GTK_TYPE_POPOVER);

static void
disconnect_signal_handlers (NautilusRenameFilePopover *self)
{
    g_assert (NAUTILUS_IS_RENAME_FILE_POPOVER (self));

    g_clear_signal_handler (&self->file_changed_handler_id, self->target_file);
}

static void
reset_state (NautilusRenameFilePopover *self)
{
    g_assert (NAUTILUS_IS_RENAME_FILE_POPOVER (self));

    disconnect_signal_handlers (self);

    g_clear_object (&self->target_file);
    self->should_rename = FALSE;

    gtk_popover_popdown (GTK_POPOVER (self));
}

static void
reveal_message (gpointer user_data)
{
    gtk_revealer_set_reveal_child (NAUTILUS_RENAME_FILE_POPOVER (user_data)->error_revealer, TRUE);
}

static void
update_state (NautilusDirectory *,
              GList *,
              gpointer user_data)
{
    NautilusRenameFilePopover *self = NAUTILUS_RENAME_FILE_POPOVER (user_data);
    g_autofree char *name = g_strstrip (g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->name_entry))));
    NautilusFileNameMessage message = nautilus_filename_message_from_name (name,
                                                                           self->containing_directory,
                                                                           self->target_file_name);
    gboolean is_valid = nautilus_filename_message_is_valid (message);
    const char *error_message = self->target_is_folder ?
                                nautilus_filename_message_folder_error (message) :
                                nautilus_filename_message_file_error (message);

    gtk_label_set_label (self->error_label, error_message);
    gtk_widget_set_sensitive (self->activate_button, is_valid);

    /* Delay showing error message if needed */
    g_clear_handle_id (&self->delayed_message_timer_id, g_source_remove);
    if (nautilus_filename_message_needs_delay (message))
    {
        self->delayed_message_timer_id = g_timeout_add_once (500, reveal_message, self);
    }
    else
    {
        gtk_revealer_set_reveal_child (self->error_revealer, error_message != NULL);
    }

    if (self->should_rename && is_valid)
    {
        /* New name is accepted, call callback */
        self->callback (self->target_file, name, gtk_widget_get_parent (GTK_WIDGET (self)));
        reset_state (self);
    }

    self->should_rename = FALSE;
}

static void
on_name_changed (NautilusRenameFilePopover *self)
{
    nautilus_directory_call_when_ready (self->containing_directory,
                                        NAUTILUS_FILE_ATTRIBUTE_INFO,
                                        TRUE,
                                        update_state,
                                        self);
}

static void
try_accept_name (NautilusRenameFilePopover *self)
{
    self->should_rename = TRUE;
    on_name_changed (self);
}

static gboolean
name_entry_on_f2_pressed (GtkWidget                 *widget,
                          NautilusRenameFilePopover *self)
{
    guint text_length;
    gint start_pos;
    gint end_pos;
    gboolean all_selected;

    text_length = (guint) gtk_entry_get_text_length (GTK_ENTRY (widget));
    if (text_length == 0)
    {
        return GDK_EVENT_STOP;
    }

    gtk_editable_get_selection_bounds (GTK_EDITABLE (widget),
                                       &start_pos, &end_pos);

    all_selected = (start_pos == 0) && ((guint) end_pos == text_length);
    if (!all_selected || !nautilus_file_is_regular_file (self->target_file))
    {
        gtk_editable_select_region (GTK_EDITABLE (widget), 0, -1);
    }
    else
    {
        gint start_offset;
        gint end_offset;

        /* Select the name part without the file extension */
        eel_filename_get_rename_region (gtk_editable_get_text (GTK_EDITABLE (widget)),
                                        &start_offset, &end_offset);
        gtk_editable_select_region (GTK_EDITABLE (widget),
                                    start_offset, end_offset);
    }

    return GDK_EVENT_STOP;
}

static gboolean
name_entry_on_undo (GtkWidget                 *widget,
                    NautilusRenameFilePopover *self)
{
    const char *edit_name = nautilus_file_get_edit_name (self->target_file);

    gtk_editable_set_text (GTK_EDITABLE (widget), edit_name);

    gtk_editable_select_region (GTK_EDITABLE (widget), 0, -1);

    return GDK_EVENT_STOP;
}

static gboolean
on_event_controller_key_key_pressed (GtkEventControllerKey *controller,
                                     guint                  keyval,
                                     guint                  keycode,
                                     GdkModifierType        state,
                                     gpointer               user_data)
{
    GtkWidget *widget;
    NautilusRenameFilePopover *self = NAUTILUS_RENAME_FILE_POPOVER (user_data);

    widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller));

    if (keyval == GDK_KEY_F2)
    {
        return name_entry_on_f2_pressed (widget, self);
    }
    else if (keyval == GDK_KEY_z && (state & GDK_CONTROL_MASK) != 0)
    {
        return name_entry_on_undo (widget, self);
    }

    return GDK_EVENT_PROPAGATE;
}

static void
target_file_on_changed (NautilusFile *file,
                        gpointer      user_data)
{
    NautilusRenameFilePopover *self = NAUTILUS_RENAME_FILE_POPOVER (user_data);

    if (nautilus_file_is_gone (file))
    {
        reset_state (self);
    }
}

NautilusRenameFilePopover *
nautilus_rename_file_popover_new (NautilusRenameCallback callback)
{
    NautilusRenameFilePopover *self = g_object_new (NAUTILUS_TYPE_RENAME_FILE_POPOVER, NULL);

    self->callback = callback;

    return self;
}

void
nautilus_rename_file_popover_show_for_file   (NautilusRenameFilePopover *self,
                                              NautilusFile              *target_file)
{
    NautilusDirectory *containing_directory;
    GtkEventController *controller;
    const char *edit_name;
    gint n_chars;

    g_assert (NAUTILUS_IS_RENAME_FILE_POPOVER (self));
    g_assert (NAUTILUS_IS_FILE (target_file));

    reset_state (self);

    self->target_file = g_object_ref (target_file);

    if (!nautilus_file_is_self_owned (self->target_file))
    {
        g_autoptr (NautilusFile) parent = NULL;

        parent = nautilus_file_get_parent (self->target_file);
        containing_directory = nautilus_directory_get_for_file (parent);
    }
    else
    {
        containing_directory = nautilus_directory_get_for_file (self->target_file);
    }

    self->target_is_folder = nautilus_file_is_directory (self->target_file);
    self->target_file_name = g_strdup (nautilus_file_get_display_name (self->target_file));
    self->containing_directory = containing_directory;

    self->file_changed_handler_id = g_signal_connect (self->target_file,
                                                      "changed",
                                                      G_CALLBACK (target_file_on_changed),
                                                      self);

    controller = gtk_event_controller_key_new ();
    gtk_widget_add_controller (self->name_entry, controller);
    g_signal_connect (controller, "key-pressed",
                      G_CALLBACK (on_event_controller_key_key_pressed), self);

    gtk_label_set_text (GTK_LABEL (self->title_label),
                        self->target_is_folder ? _("Rename Folder") :
                                                 _("Rename File"));

    edit_name = nautilus_file_get_edit_name (self->target_file);

    gtk_editable_set_text (GTK_EDITABLE (self->name_entry), edit_name);

    if (nautilus_file_is_regular_file (self->target_file))
    {
        gint start_offset;
        gint end_offset;

        /* Select the name part without the file extension */
        eel_filename_get_rename_region (edit_name,
                                        &start_offset, &end_offset);
        gtk_editable_select_region (GTK_EDITABLE (self->name_entry),
                                    start_offset, end_offset);
    }

    n_chars = g_utf8_strlen (edit_name, -1);
    gtk_editable_set_width_chars (GTK_EDITABLE (self->name_entry),
                                  MIN (MAX (n_chars, RENAME_ENTRY_MIN_CHARS),
                                       RENAME_ENTRY_MAX_CHARS));
}

static void
nautilus_rename_file_popover_init (NautilusRenameFilePopover *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));
}

static void
rename_file_popover_dispose (GObject *object)
{
    gtk_widget_dispose_template (GTK_WIDGET (object), NAUTILUS_TYPE_RENAME_FILE_POPOVER);

    G_OBJECT_CLASS (nautilus_rename_file_popover_parent_class)->dispose (object);
}

static void
nautilus_rename_file_popover_finalize (GObject *object)
{
    NautilusRenameFilePopover *self = NAUTILUS_RENAME_FILE_POPOVER (object);

    reset_state (self);

    g_clear_object (&self->containing_directory);
    g_free (self->target_file_name);

    G_OBJECT_CLASS (nautilus_rename_file_popover_parent_class)->finalize (object);
}

static void
nautilus_rename_file_popover_class_init (NautilusRenameFilePopoverClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = nautilus_rename_file_popover_finalize;
    object_class->dispose = rename_file_popover_dispose;

    gtk_widget_class_set_template_from_resource (widget_class,
                                                 "/org/gnome/nautilus/ui/nautilus-rename-file-popover.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusRenameFilePopover, activate_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusRenameFilePopover, error_label);
    gtk_widget_class_bind_template_child (widget_class, NautilusRenameFilePopover, error_revealer);
    gtk_widget_class_bind_template_child (widget_class, NautilusRenameFilePopover, name_entry);
    gtk_widget_class_bind_template_child (widget_class, NautilusRenameFilePopover, title_label);

    gtk_widget_class_bind_template_callback (widget_class, on_name_changed);
    gtk_widget_class_bind_template_callback (widget_class, reset_state);
    gtk_widget_class_bind_template_callback (widget_class, try_accept_name);
}