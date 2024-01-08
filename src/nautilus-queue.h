/*
   Copyright (C) 2001 Maciej Stachowiak

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, see <http://www.gnu.org/licenses/>.

   Author: Maciej Stachowiak <mjs@noisehavoc.org>
*/

#pragma once

#include "nautilus-file.h"

typedef struct NautilusQueue NautilusQueue;

NautilusQueue     *nautilus_queue_new      (GHashFunc      hash_func,
                                            GEqualFunc     equal_func,
                                            GDestroyNotify free_func);
void               nautilus_queue_destroy  (NautilusQueue *queue);

/* Add a file to the tail of the queue, unless it's already in the queue */
gboolean           nautilus_queue_enqueue  (NautilusQueue *queue,
                                            gpointer       item);

/* Return the file at the head of the queue after removing it from the
 * queue. This is dangerous unless you have another ref to the file,
 * since it will unref it if the free_func is set.
 */
gpointer           nautilus_queue_dequeue  (NautilusQueue *queue);

/* Remove a file from an arbitrary point in the queue in constant time. */
void               nautilus_queue_remove   (NautilusQueue *queue,
                                            gconstpointer  item);

/* Get the file at the head of the queue without removing or unrefing it. */
gpointer           nautilus_queue_head     (NautilusQueue *queue);

gboolean           nautilus_queue_is_empty (NautilusQueue *queue);

void               nautilus_queue_move_existing_to_head (NautilusQueue *queue,
                                                         gconstpointer  item);

gpointer           nautilus_queue_find_item             (NautilusQueue *queue,
                                                         gpointer       key);

guint              nautilus_queue_get_length            (NautilusQueue *queue);
