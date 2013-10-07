/********************************************************************\
 * gnc-gsettings.c -- utility functions for storing/retrieving      *
 *              data in the GSettings database for GnuCash          *
 * Copyright (C) 2013 Geert Janssens <geert@kobaltwit.be>           *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include "gnc-gsettings.h"
#include "gnc-path.h"
#include "guile-mappings.h"
#include <libguile.h>
#include "libqof/qof/qof.h"
#include "gnc-prefs-p.h"

#include <libxml/xmlmemory.h>
#include <libxml/debugXML.h>
#include <libxml/HTMLtree.h>
#include <libxml/xmlIO.h>
#include <libxml/xinclude.h>
#include <libxml/catalog.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

#define CLIENT_TAG  "%s-%s-client"
#define NOTIFY_TAG  "%s-%s-notify_id"

static GHashTable *schema_hash = NULL;
static const gchar *gsettings_prefix;
static xmlExternalEntityLoader defaultEntityLoader = NULL;

/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = G_LOG_DOMAIN;

/************************************************************/
/*               Internal helper functions                  */
/************************************************************/
static gboolean gnc_gsettings_is_valid_key(GSettings *settings, const gchar *key)
{
    gchar **keys = NULL;
    gint i = 0;
    gboolean found = FALSE;

    // Check if the key is valid key within settings
    if (!G_IS_SETTINGS(settings))
        return FALSE;

    // Get list of keys
    keys = g_settings_list_keys(settings);

    while (keys && keys[i])
    {
        if (!g_strcmp0(key, keys[i]))
        {
            found = TRUE;
            break;
        }
        i++;
    }

    // Free keys
    g_strfreev(keys);

    return found;
}

static GSettings * gnc_gsettings_get_schema_ptr (const gchar *schema_str)
{
    GSettings *gset = NULL;
    gchar *full_name = gnc_gsettings_normalize_schema_name (schema_str);

    ENTER("");
    if (!schema_hash)
        schema_hash = g_hash_table_new (g_str_hash, g_str_equal);

    gset = g_hash_table_lookup (schema_hash, full_name);
    DEBUG ("Looking for schema %s returned gsettings %p", full_name, gset);
    if (!gset)
    {
        gset = g_settings_new (full_name);
        DEBUG ("Created gsettings object %p for schema %s", gset, full_name);
        if (G_IS_SETTINGS(gset))
            g_hash_table_insert (schema_hash, full_name, gset);
        else
            PWARN ("Ignoring attempt to access unknown gsettings schema %s", full_name);
    }

    LEAVE("");
    return gset;
}


/************************************************************/
/*                      GSettings Utilities                 */
/************************************************************/

void
gnc_gsettings_set_prefix (const gchar *prefix)
{
    gsettings_prefix = prefix;
}

const gchar *
gnc_gsettings_get_prefix (void)
{
    return gsettings_prefix;
}

gchar *
gnc_gsettings_normalize_schema_name (const gchar *name)
{
    if (name == NULL)
    {
        /* Need to return a newly allocated string */
        return g_strdup(gnc_gsettings_get_prefix());
    }
    if (g_str_has_prefix (name, gnc_gsettings_get_prefix ()))
    {
        /* Need to return a newly allocated string */
        return g_strdup(name);
    }

    return g_strjoin(".", gnc_gsettings_get_prefix(), name, NULL);
}


/************************************************************/
/*                   Change notification                    */
/************************************************************/

gulong
gnc_gsettings_register_cb (const gchar *schema,
                           const gchar *key,
                           gpointer func,
                           gpointer user_data)
{
    gulong retval = 0;
    gchar *signal = NULL;

    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);

    ENTER("");
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), retval);
    g_return_val_if_fail (func, retval);

    if ((!key) || (*key == '\0'))
        signal = g_strdup ("changed");
    else
    {
        if (gnc_gsettings_is_valid_key(schema_ptr, key))
            signal = g_strconcat ("changed::", key, NULL);
    }

    retval = g_signal_connect (schema_ptr, signal, G_CALLBACK (func), user_data);

    g_free (signal);

    LEAVE("");
    return retval;
}


void
gnc_gsettings_remove_cb_by_func (const gchar *schema,
                                 const gchar *key,
                                 gpointer func,
                                 gpointer user_data)
{
    gint matched = 0;
    gchar *signal = NULL;

    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_if_fail (G_IS_SETTINGS (schema_ptr));
    g_return_if_fail (func);

    if ((!key) || (*key == '\0'))
        signal = g_strdup ("changed");
    else
    {
        if (gnc_gsettings_is_valid_key(schema_ptr, key))
            signal = g_strconcat ("changed::", key, NULL);
    }

    matched = g_signal_handlers_disconnect_matched (
            schema_ptr,
            G_SIGNAL_MATCH_DETAIL ||G_SIGNAL_MATCH_FUNC || G_SIGNAL_MATCH_DATA,
            0, /* signal_id */
            g_quark_from_string (signal),   /* signal_detail */
            NULL, /* closure */
            G_CALLBACK (func), /* callback function */
            user_data);
    DEBUG ("Removed %d handlers for signal '%s' from schema '%s'", matched, signal, schema);

    g_free (signal);
}


void
gnc_gsettings_remove_cb_by_id (const gchar *schema,
                               guint handlerid)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_if_fail (G_IS_SETTINGS (schema_ptr));

    g_signal_handler_disconnect (schema_ptr, handlerid);
}


guint
gnc_gsettings_register_any_cb (const gchar *schema,
                               gpointer func,
                               gpointer user_data)
{
    return gnc_gsettings_register_cb (schema, NULL, func, user_data);
}


void
gnc_gsettings_remove_any_cb_by_func (const gchar *schema,
                                     gpointer func,
                                     gpointer user_data)
{
    gnc_gsettings_remove_cb_by_func (schema, NULL, func, user_data);
}


void gnc_gsettings_bind (const gchar *schema,
                         /*@ null @*/ const gchar *key,
                         gpointer object,
                         const gchar *property)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_if_fail (G_IS_SETTINGS (schema_ptr));

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        g_settings_bind (schema_ptr, key, object, property, 0);
    else
    {
        PERR ("Invalid key %s for schema %s", key, schema);
    }
}

/************************************************************/
/*                      Getters/Setters                     */
/************************************************************/

gboolean
gnc_gsettings_get_bool (const gchar *schema,
                        const gchar *key)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), FALSE);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        return g_settings_get_boolean (schema_ptr, key);
    else
    {
        PERR ("Invalid key %s for schema %s", key, schema);
        return FALSE;
    }
}

gboolean
gnc_gsettings_set_bool (const gchar *schema,
                        const gchar *key,
                        gboolean value)
{
    gboolean result = FALSE;
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), FALSE);

    ENTER("schema: %s, key: %s", schema, key);
    if (gnc_gsettings_is_valid_key (schema_ptr, key))
    {
        result = g_settings_set_boolean (schema_ptr, key, value);
        if (!result)
            PERR ("Unable to set value for key %s in schema %s", key, schema);
    }
    else
        PERR ("Invalid key %s for schema %s", key, schema);

    LEAVE("result %i", result);
    return result;
}

gint
gnc_gsettings_get_int (const gchar *schema,
                       const gchar *key)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), 0);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        return g_settings_get_int (schema_ptr, key);
    else
    {
        PERR ("Invalid key %s for schema %s", key, schema);
        return 0;
    }
}

gboolean
gnc_gsettings_set_int (const gchar *schema,
                       const gchar *key,
                       gint value)
{
    gboolean result = FALSE;
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), FALSE);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
    {
        result = g_settings_set_int (schema_ptr, key, value);
        if (!result)
            PERR ("Unable to set value for key %s in schema %s", key, schema);
    }
    else
        PERR ("Invalid key %s for schema %s", key, schema);

    return result;
}

gdouble
gnc_gsettings_get_float (const gchar *schema,
                         const gchar *key)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), 0);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        return g_settings_get_double (schema_ptr, key);
    else
    {
        PERR ("Invalid key %s for schema %s", key, schema);
        return 0;
    }
}

gboolean
gnc_gsettings_set_float (const gchar *schema,
                         const gchar *key,
                         gdouble value)
{
    gboolean result = FALSE;
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), FALSE);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
    {
        result = g_settings_set_double (schema_ptr, key, value);
        if (!result)
            PERR ("Unable to set value for key %s in schema %s", key, schema);
    }
    else
        PERR ("Invalid key %s for schema %s", key, schema);

    return result;
}

gchar *
gnc_gsettings_get_string (const gchar *schema,
                          const gchar *key)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), NULL);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        return g_settings_get_string (schema_ptr, key);
    else
    {
        PERR ("Invalid key %s for schema %s", key, schema);
        return NULL;
    }
}

gboolean
gnc_gsettings_set_string (const gchar *schema,
                          const gchar *key,
                          const gchar *value)
{
    gboolean result = FALSE;
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), FALSE);

    ENTER("schema: %s, key: %s", schema, key);
    if (gnc_gsettings_is_valid_key (schema_ptr, key))
    {
        result = g_settings_set_string (schema_ptr, key, value);
        if (!result)
            PERR ("Unable to set value for key %s in schema %s", key, schema);
    }
    else
        PERR ("Invalid key %s for schema %s", key, schema);

    LEAVE("result %i", result);
    return result;
}

gint
gnc_gsettings_get_enum (const gchar *schema,
                        const gchar *key)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), 0);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        return g_settings_get_enum (schema_ptr, key);
    else
    {
        PERR ("Invalid key %s for schema %s", key, schema);
        return 0;
    }
}

gboolean
gnc_gsettings_set_enum (const gchar *schema,
                        const gchar *key,
                        gint value)
{
    gboolean result = FALSE;
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), FALSE);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
    {
        result = g_settings_set_enum (schema_ptr, key, value);
        if (!result)
            PERR ("Unable to set value for key %s in schema %s", key, schema);
    }
    else
        PERR ("Invalid key %s for schema %s", key, schema);

    return result;
}

GVariant *
gnc_gsettings_get_value (const gchar *schema,
                         const gchar *key)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), NULL);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        return g_settings_get_value (schema_ptr, key);
    else
    {
        PERR ("Invalid key %s for schema %s", key, schema);
        return NULL;
    }
}

gboolean
gnc_gsettings_set_value (const gchar *schema,
                         const gchar *key,
                         GVariant *value)
{
    gboolean result = FALSE;
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_val_if_fail (G_IS_SETTINGS (schema_ptr), FALSE);

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
    {
        result = g_settings_set_value (schema_ptr, key, value);
        if (!result)
            PERR ("Unable to set value for key %s in schema %s", key, schema);
    }
    else
        PERR ("Invalid key %s for schema %s", key, schema);

    return result;
}

void
gnc_gsettings_reset (const gchar *schema,
                     const gchar *key)
{
    GSettings *schema_ptr = gnc_gsettings_get_schema_ptr (schema);
    g_return_if_fail (G_IS_SETTINGS (schema_ptr));

    if (gnc_gsettings_is_valid_key (schema_ptr, key))
        g_settings_reset (schema_ptr, key);
    else
        PERR ("Invalid key %s for schema %s", key, schema);
}

void
gnc_gsettings_reset_schema (const gchar *schema)
{
    gchar **keys;
    gint counter = 0;

    keys = g_settings_list_keys (gnc_gsettings_get_schema_ptr (schema));

    if (!keys)
        return;

    while (keys[counter])
    {
        gnc_gsettings_reset (schema, keys[counter]);
        counter++;
    }

    g_strfreev (keys);
}

void gnc_gsettings_load_backend (void)
{
    ENTER("");
    prefsbackend.register_cb = gnc_gsettings_register_cb;
    prefsbackend.remove_cb_by_func = gnc_gsettings_remove_cb_by_func;
    prefsbackend.remove_cb_by_id = gnc_gsettings_remove_cb_by_id;
    prefsbackend.register_group_cb = gnc_gsettings_register_any_cb;
    prefsbackend.remove_group_cb_by_func = gnc_gsettings_remove_any_cb_by_func;
    prefsbackend.bind = gnc_gsettings_bind;
    prefsbackend.get_bool = gnc_gsettings_get_bool;
    prefsbackend.get_int = gnc_gsettings_get_int;
    prefsbackend.get_float = gnc_gsettings_get_float;
    prefsbackend.get_string = gnc_gsettings_get_string;
    prefsbackend.get_enum = gnc_gsettings_get_enum;
    prefsbackend.get_value = gnc_gsettings_get_value;
    prefsbackend.set_bool = gnc_gsettings_set_bool;
    prefsbackend.set_int = gnc_gsettings_set_int;
    prefsbackend.set_float = gnc_gsettings_set_float;
    prefsbackend.set_string = gnc_gsettings_set_string;
    prefsbackend.set_enum = gnc_gsettings_set_enum;
    prefsbackend.set_value = gnc_gsettings_set_value;
    prefsbackend.reset = gnc_gsettings_reset;
    prefsbackend.reset_group = gnc_gsettings_reset_schema;

    LEAVE("Prefsbackend bind = %p", prefsbackend.bind);
}

/* Attempt to migrate preferences from gconf files
    to gsettings if not already done so */

/* This snippet is borrowed from the xsltproc source
 * and adapted to help the xsl transform find our temporary
 * files in $HOME/.gnc-migration-tmp/
 */
static xmlParserInputPtr
xsltprocExternalEntityLoader(const char *URL, const char *ID,
                             xmlParserCtxtPtr ctxt) {
    xmlParserInputPtr ret;
    warningSAXFunc warning = NULL;
        xmlChar *newURL;
    gchar *tmpdir = g_build_filename (g_getenv ("HOME"), ".gnc-migration-tmp", NULL);

    int i;
    const char *lastsegment = URL;
    const char *iter = URL;

    while (*iter != 0) {
        if (*iter == '/')
            lastsegment = iter + 1;
        iter++;
    }

    if ((ctxt != NULL) && (ctxt->sax != NULL)) {
        warning = ctxt->sax->warning;
        ctxt->sax->warning = NULL;
    }

    if (defaultEntityLoader != NULL) {
        ret = defaultEntityLoader(URL, ID, ctxt);
        if (ret != NULL) {
            if (warning != NULL)
                ctxt->sax->warning = warning;
            return(ret);
        }
    }

    newURL = xmlStrdup((const xmlChar *) tmpdir);
    newURL = xmlStrcat(newURL, (const xmlChar *) "/");
    newURL = xmlStrcat(newURL, (const xmlChar *) lastsegment);
    g_free (tmpdir);
    if (newURL != NULL) {
        ret = defaultEntityLoader((const char *)newURL, ID, ctxt);
        if (ret != NULL) {
            if (warning != NULL)
                ctxt->sax->warning = warning;
            xmlFree(newURL);
            return(ret);
        }
        xmlFree(newURL);
    }
    if (warning != NULL) {
        ctxt->sax->warning = warning;
        if (URL != NULL)
            warning(ctxt, "failed to load external entity \"%s\"\n", URL);
        else if (ID != NULL)
            warning(ctxt, "failed to load external entity \"%s\"\n", ID);
    }
    return(NULL);
}


void gnc_gsettings_migrate_from_gconf (void)
{
    gchar *pkgdatadir, *stylesheet, *input, *output;
    gchar *migr_dir;
    SCM migr_script;
    SCM result = scm_c_eval_string ("(use-modules (gnucash app-utils))(migration-prepare)");
    xsltStylesheetPtr stylesheetptr = NULL;
    xmlDocPtr inputxml, transformedxml;
    FILE *outfile;

    pkgdatadir = gnc_path_get_pkgdatadir();
    stylesheet = g_build_filename(pkgdatadir, "make-prefs-migration-script.xsl", NULL);
    input      = g_build_filename(pkgdatadir, "migratable-prefs.xml", NULL);
    migr_dir   = g_build_filename(g_getenv ("HOME"), ".gnc-migration-tmp", NULL);
    output     = g_build_filename(migr_dir, "migrate-prefs-user.scm", NULL);
    xmlSubstituteEntitiesDefault(1);
    xmlLoadExtDtdDefaultValue = 1;
    defaultEntityLoader = xmlGetExternalEntityLoader();
    xmlSetExternalEntityLoader(xsltprocExternalEntityLoader);
    stylesheetptr = xsltParseStylesheetFile((const xmlChar *)stylesheet);
    inputxml = xmlParseFile(input);
    transformedxml = xsltApplyStylesheet(stylesheetptr, inputxml, NULL);

    outfile = fopen(output, "w");
    xsltSaveResultToFile(outfile, transformedxml, stylesheetptr);
    fclose(outfile);

    migr_script = scm_from_locale_string (output);
    scm_primitive_load (migr_script);
    result = scm_c_eval_string ("(use-modules (migrate-prefs-user))(run-migration)");

    xsltFreeStylesheet(stylesheetptr);
    xmlFreeDoc(inputxml);
    xmlFreeDoc(transformedxml);

    xsltCleanupGlobals();
    xmlCleanupParser();

    result = scm_c_eval_string ("(use-modules (gnucash app-utils))(migration-cleanup)");

    g_free (pkgdatadir);
    g_free (stylesheet);
    g_free (input);
    g_free (output);
    g_free (migr_dir);

}
