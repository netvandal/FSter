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

#include "hierarchy.h"
#include "property-handler.h"
#include "utils.h"

#define DEFAULT_SAVE_PATH               "~/.fster_saving"

static GList                            *LoadedContentsPlugins      = NULL;
static HierarchyNode                    *ExposingTree               = NULL;
static NodesCache                       *Cache                      = NULL;
static GHashTable                       *Params                     = NULL;

static void create_dummy_references ()
{
    gchar *path;

    path = DUMMY_FILEPATH;
    fclose (fopen (path, "w"));

    mkdir (FAKE_SAVING_FOLDER, 0770);
    mkdir (DUMMY_DIRPATH, 0770);
}

static void load_plugins ()
{
    int n;
    register int i;
    gchar *path;
    gchar *plug_path;
    void *plug_handler;
    GType (*plugin_registrar) ();
    struct dirent **namelist;

    path = PLUGIN_DIR;

    if (access (path, F_OK) != 0) {
        g_warning ("Unable to access contents plugin folder, expected in %s", path);
    }
    else {
        n = scandir (path, &namelist, 0, alphasort);

        for (i = 0; i < n; i++) {
            if (g_str_has_suffix (namelist [i]->d_name, ".so") == TRUE) {
                plug_path = g_build_filename (path, namelist [i]->d_name, NULL);
                plug_handler = dlopen (plug_path, RTLD_LAZY | RTLD_GLOBAL);

                if (plug_handler == NULL) {
                    g_warning ("Unable to open module in %s", plug_path);
                    g_free (plug_path);
                    continue;
                }

                g_free (plug_path);
                plugin_registrar = dlsym (plug_handler, "load_contents_plugin_type");
                LoadedContentsPlugins = g_list_prepend (LoadedContentsPlugins, g_object_new (plugin_registrar (), NULL));
                // dlclose (plug_handler);
            }

            free (namelist [i]);
        }

        free (namelist);
    }
}

void build_hierarchy_tree_from_xml (xmlDocPtr doc)
{
    gboolean saving_set;
    gchar *str;
    xmlNode *root;
    xmlNode *node;

    root = xmlDocGetRootElement (doc);
    if (strcmp ((gchar*) root->name, "conf") != 0) {
        g_warning ("Error: configuration file must be enclosed in a 'conf' tag, '%s' is found", root->name);
        return;
    }

    properties_pool_init ();
    load_plugins ();
    saving_set = FALSE;

    for (node = root->children; node; node = node->next) {
        if (strcmp ((gchar*) node->name, "exposing_tree") == 0) {
            ExposingTree = hierarchy_node_new_from_xml (NULL, node->children);
        }
        else if (strcmp ((gchar*) node->name, "saving_tree") == 0) {
            /**
                TODO    Handle complete saving-tree
            */

            str = (gchar*) xmlGetProp (node, (xmlChar*) "base_path");
            if (str != NULL) {
                hierarchy_node_set_save_path (str);
                xmlFree (str);
                saving_set = TRUE;
            }
        }
        else {
            g_warning ("Error: unrecognized tag '%s'", (gchar*) node->name);
        }
    }

    if (saving_set == FALSE)
        hierarchy_node_set_save_path (DEFAULT_SAVE_PATH);

    create_dummy_references ();
    Cache = nodes_cache_new ();
}

void destroy_hierarchy_tree ()
{
    g_object_unref (Cache);
    g_object_unref (ExposingTree);
    hierarchy_node_set_save_path (NULL);
    properties_pool_finish ();
}

static GList* tokenize_path (const gchar *path)
{
    gchar *token;
    gchar *dirs;
    GList *list;

    list = NULL;
    dirs = strdupa (path);

    for (;;) {
        token = basename (dirs);
        if (token [0] == '/')
            break;

        list = g_list_prepend (list, g_strdup (token));
        dirs = dirname (dirs);
    }

    return list;
}

static ItemHandler* root_item ()
{
    static ItemHandler *ret     = NULL;

    if (ret == NULL) {
        ret = g_object_new (ITEM_HANDLER_TYPE,
                            "type", ITEM_IS_STATIC_FOLDER,
                            "node", ExposingTree,
                            "file_path", getenv ("HOME"),
                            "exposed_name", "/", NULL);
    }

    return ret;
}

static ItemHandler* search_exposed_name_in_list (GList *items, const gchar *searchname)
{
    GList *iter;
    ItemHandler *item;
    ItemHandler *cmp;

    item = NULL;

    for (iter = items; iter; iter = g_list_next (iter)) {
        cmp = (ItemHandler*) iter->data;
        if (strcmp (item_handler_exposed_name (cmp), searchname) == 0) {
            item = cmp;
            break;
        }
    }

    return item;
}

ItemHandler* verify_exposed_path_in_folder (HierarchyNode *level, ItemHandler *root, const gchar *path) {
    GList *children;
    ItemHandler *ret;

    if (level == NULL)
        children = item_handler_get_children (root);
    else
        children = hierarchy_node_get_subchildren (level, root);

    if (children == NULL)
        return NULL;

    ret = search_exposed_name_in_list (children, path);
    g_list_free (children);
    return ret;
}

ItemHandler* verify_exposed_path (const gchar *path)
{
    GList *path_tokens;
    GList *iter;
    HierarchyNode *level;
    ItemHandler *item;

    item = nodes_cache_get_by_path (Cache, path);
    if (item != NULL)
        return item;

    if (strcmp (path, "/") == 0) {
        item = root_item ();
    }
    else {
        if (ExposingTree == NULL) {
            g_warning ("Warning: there is not an exposing hierarchy for item handler");
            return NULL;
        }

        path_tokens = tokenize_path (path);

        item = NULL;
        iter = NULL;
        level = ExposingTree;

        /*
            Note: here is not suggested to directly check existing files if the node is a
            mirror_content, because the complete chain of parents items is required when (for
            example) creating a new file (to rebuild the complete real path to touch). Perhaps is
            a good idea to shorthand here access to directly mapped filesystem, but adopt a
            different strategy in ifs_create()
        */

        for (iter = path_tokens; iter; iter = g_list_next (iter)) {
            item = verify_exposed_path_in_folder (level, item, (const gchar*) iter->data);
            if (item == NULL)
                break;

            level = item_handler_get_logic_node (item);
        }

        easy_list_free (path_tokens);
    }

    if (item != NULL)
        nodes_cache_set_by_path (Cache, item, g_strdup (path));

    return item;
}

HierarchyNode* node_at_path (const gchar *path)
{
    GList *path_tokens;
    GList *iter;
    HierarchyNode *level;
    ItemHandler *item;

    item = nodes_cache_get_by_path (Cache, path);
    if (item != NULL)
        return item_handler_get_logic_node (item);

    if (strcmp (path, "/") == 0) {
        level = ExposingTree;
    }
    else {
        if (ExposingTree == NULL) {
            g_warning ("Warning: there is not an exposing hierarchy for hierarchy node");
            return NULL;
        }

        path_tokens = tokenize_path (path);

        item = NULL;
        iter = NULL;
        level = ExposingTree;

        for (iter = path_tokens; iter; iter = g_list_next (iter)) {
            item = verify_exposed_path_in_folder (level, item, (const gchar*) iter->data);
            if (item == NULL)
                break;

            level = item_handler_get_logic_node (item);
        }

        easy_list_free (path_tokens);
    }

    return level;
}

void replace_hierarchy_node (ItemHandler *old_item, ItemHandler *new_item)
{
    int first;
    int second;
    gchar *buffer;
    struct stat sbuf;

    if (item_handler_stat (old_item, &sbuf) != 0)
        return;

    first = item_handler_open (old_item, O_RDONLY);
    second = item_handler_open (new_item, O_WRONLY);

    buffer = alloca (sbuf.st_size);
    read (first, buffer, sbuf.st_size);
    write (second, buffer, sbuf.st_size);

    item_handler_close (old_item, first);
    item_handler_close (new_item, second);

    /**
        TODO    Perhaps also metadata have to be moved?
    */

    item_handler_remove (old_item);
}

ContentsPlugin* retrieve_contents_plugin (gchar *name)
{
    const gchar *plug_name;
    GList *iter;
    ContentsPlugin *plug;

    plug = NULL;

    for (iter = LoadedContentsPlugins; iter; iter = g_list_next (iter)) {
        plug = (ContentsPlugin*) iter->data;
        plug_name = contents_plugin_get_name (plug);

        if (strcmp (plug_name, name) == 0)
            break;
    }

    return plug;
}

NodesCache* get_cache_reference ()
{
    return Cache;
}

void set_user_param (gchar *name, gchar *value)
{
    if (Params == NULL)
        Params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    if (name == NULL && value == NULL) {
        g_hash_table_destroy (Params);
        Params = NULL;
        return;
    }

    g_hash_table_insert (Params, name, value);
}

const gchar* get_user_param (gchar *name)
{
    if (Params == NULL)
        return NULL;

    return (const gchar*) g_hash_table_lookup (Params, name);
}
