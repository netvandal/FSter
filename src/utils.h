/*  Copyright (C) 2009 Itsme S.r.L.
 *  Copyright (C) 2012 Roberto Guido <roberto.guido@linux.it>
 *
 *  This file is part of FSter
 *
 *  FSter is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"

void                easy_list_free                          (GList *list);
gchar*              from_glist_to_string                    (GList *strings, const gchar *separator, gboolean free_list);
void                check_and_create_folder                 (gchar *path);
void                create_file                             (gchar *path);
GVariant*           execute_query                           (gchar *query, GError **error);
void                execute_update                          (gchar *query, GError **error);
GVariant*           execute_update_blank                    (gchar *query, GError **error);

