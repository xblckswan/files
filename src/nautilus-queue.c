/*
 *  Copyright (C) 2001 Maciej Stachowiak
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Author: Maciej Stachowiak <mjs@noisehavoc.org>
 */

#include <config.h>
#include "nautilus-queue.h"

#include <glib.h>

/* NautilusQueue is essentially a GQueue with 2 special features
 * 1) It can find and remove items in constant time
 * 2) It doesn't allow duplicates.
 */

struct NautilusQueue
{
    GQueue *data;
    GHashTable *item_to_link_map;
};

NautilusQueue *
nautilus_queue_new (GHashFunc      hash_func,
                    GEqualFunc     equal_func,
                    GDestroyNotify free_func)
{
    NautilusQueue *queue;

    queue = g_new0 (NautilusQueue, 1);
    queue->data = g_queue_new ();
    queue->item_to_link_map = g_hash_table_new_full (hash_func, equal_func, free_func, NULL);

    return queue;
}

void
nautilus_queue_destroy (NautilusQueue *queue)
{
    g_hash_table_destroy (queue->item_to_link_map);
    g_queue_free (queue->data);   /* Items already freed by HashTable */
    g_free (queue);
}

gboolean
nautilus_queue_enqueue (NautilusQueue *queue,
                        gpointer       item)
{
    GList *link;

    if (g_hash_table_lookup (queue->item_to_link_map, item) != NULL)
    {
        /* It's already on the queue. */
        return FALSE;
    }

    link = g_list_prepend (NULL, item);
    g_queue_push_tail_link (queue->data, link);
    g_hash_table_insert (queue->item_to_link_map, item, link);

    return TRUE;
}

gpointer
nautilus_queue_dequeue (NautilusQueue *queue)
{
    gpointer item = nautilus_queue_head (queue);

    nautilus_queue_remove (queue, item);

    return item;
}


void
nautilus_queue_remove (NautilusQueue *queue,
                       gconstpointer  item)
{
    GList *link;

    link = g_hash_table_lookup (queue->item_to_link_map, item);

    if (link == NULL)
    {
        /* It's not on the queue */
        return;
    }

    g_queue_delete_link (queue->data, link);
    g_hash_table_remove (queue->item_to_link_map, item);
}

gpointer
nautilus_queue_head (NautilusQueue *queue)
{
    return g_queue_peek_head (queue->data);
}

gboolean
nautilus_queue_is_empty (NautilusQueue *queue)
{
    return g_queue_is_empty (queue->data);
}

void
nautilus_queue_move_existing_to_head (NautilusQueue *queue,
                                      gconstpointer  item)
{
    GList *link = g_hash_table_lookup (queue->item_to_link_map, item);

    if (link == NULL)
    {
        return;
    }

    g_queue_unlink (queue->data, link);
    g_queue_push_head_link (queue->data, link);
}

gpointer
nautilus_queue_find_item (NautilusQueue *queue,
                          gpointer       key)
{
    GList *link = g_hash_table_lookup (queue->item_to_link_map, key);

    if (link == NULL)
    {
        return NULL;
    }

    return link->data;
}

guint
nautilus_queue_get_length (NautilusQueue *queue)
{
    return g_queue_get_length (queue->data);
}
