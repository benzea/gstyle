#include "gstylesheet.h"

#include <glib.h>
#include <libcroco/libcroco.h>

G_DEFINE_TYPE (GStylesheet, g_stylesheet, G_TYPE_OBJECT)

#define G_STYLESHEET_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), G_TYPE_STYLESHEET, GStylesheetPrivate))

typedef struct _GStylesheetPrivate GStylesheetPrivate;

struct _GStylesheetPrivate
{
  CRStyleSheet *stylesheet;
  CRNodeIface   cr_iface;
};

typedef struct
{
  CRNode node;
  GStyleable *styleable;
} GStyleableCRNode;

static CRNode*     _styleable_get_parent_node      (CRNode const *node);
static CRNode*     _styleable_first_child          (CRNode const *node);
static CRNode*     _styleable_get_next_sibling     (CRNode const *node);
static CRNode*     _styleable_get_previous_sibling (CRNode const *node);
static char const* _styleable_get_node_name        (CRNode const *node);
static char*       _styleable_get_attribute        (CRNode const *node,
                                                   const char* attribute);
static guint       _styleable_get_children_count   (CRNode const *node);
static guint       _styleable_get_index            (CRNode const *node);
static void        _styleable_release              (CRNode *node);

static CRNode*
_get_cr_node_from_g_styleable (GStyleable *styleable, CRNodeIface *cr_iface)
{
  GObject *object = G_OBJECT (styleable);
  GStyleableCRNode *node = NULL;

  if (styleable == NULL)
    return NULL;

  node = g_object_get_data (object, "g_styleable_cr_node");
  if (node == NULL)
    {
      /* XXX: Use GSlice instead. */
      node = g_new (GStyleableCRNode, 1);
      node->node.iface = cr_iface;
      node->styleable = styleable;
      g_object_set_data_full (object, "g_styleable_cr_node", node, g_free);
    }

  g_object_ref (G_OBJECT (styleable));
  return (CRNode*) node;
}

static GStyleable*
_g_styleable_get_from_cr_node (CRNode const *node)
{
  return ((GStyleableCRNode*) node)->styleable;
}

static void
g_stylesheet_dispose (GObject *object)
{
  G_OBJECT_CLASS (g_stylesheet_parent_class)->dispose (object);
}

static void
g_stylesheet_finalize (GObject *object)
{
  G_OBJECT_CLASS (g_stylesheet_parent_class)->finalize (object);
}

static void
g_stylesheet_class_init (GStylesheetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GStylesheetPrivate));

  object_class->dispose = g_stylesheet_dispose;
  object_class->finalize = g_stylesheet_finalize;
}

static void
g_stylesheet_init (GStylesheet *self)
{
  GStylesheetPrivate *priv;

  priv = G_STYLESHEET_GET_PRIVATE (self);

  priv->stylesheet = NULL;

  priv->cr_iface.get_parent_node = _styleable_get_parent_node;
  priv->cr_iface.get_first_child = _styleable_first_child;
  priv->cr_iface.get_next_sibling = _styleable_get_next_sibling;
  priv->cr_iface.get_previous_sibling = _styleable_get_previous_sibling;
  priv->cr_iface.get_node_name = _styleable_get_node_name;
  priv->cr_iface.get_attribute = _styleable_get_attribute;
  priv->cr_iface.get_children_count = _styleable_get_children_count;
  priv->cr_iface.get_index = _styleable_get_index;
  priv->cr_iface.release = _styleable_release;
}

GStylesheet*
g_stylesheet_new (void)
{
  /*
  static GStylesheet *singleton = NULL;

  if (!singleton)
    {
      singleton = g_object_new (G_TYPE_STYLESHEET, NULL);

      return singleton;
    }

  return singleton;
  */

  return g_object_new (G_TYPE_STYLESHEET, NULL);
}

GStylesheet* g_stylesheet_new_from_file (const gchar *filename,
                                         GError      **error)
{
  GStylesheet *stylesheet;
  GStylesheetPrivate *priv;
  GIOChannel *file;
  GIOStatus gio_status;
  gchar *buffer;
  gsize length;

  buffer = NULL;
  stylesheet = NULL;

  stylesheet = g_stylesheet_new ();
  
  priv = G_STYLESHEET_GET_PRIVATE (stylesheet);

  file = g_io_channel_new_file (filename, "r", error);

  if (file)
    {
      gio_status = g_io_channel_read_to_end (file, &buffer, &length, error);

      gio_status = g_io_channel_shutdown (file, FALSE, error);

      if (!error)
        {
          enum CRStatus status;

          status = cr_om_parser_simply_parse_buf ((guchar*)buffer,
                                                  (gulong)length,
                                                  CR_ASCII,
                                                  &priv->stylesheet);

          g_debug ("> STATUS: %d %" G_GSIZE_MODIFIER "d\n", 
                   status == CR_OK,
                   length);

          cr_stylesheet_dump (priv->stylesheet, stdout);

          g_print ("\n\n");
        }
    }

  return stylesheet;
}

gboolean
g_stylesheet_get_property (GStylesheet  *stylesheet,
                           GStyleable    *styleable,
                           const gchar  *property_name,
                           gchar        **property)
{
  GStylesheetPrivate *priv;
  CRStatement **stmts_tab = NULL;
  CRNode *node;
  gulong length;
  CRSelEng *engine;
  enum CRStatus status;
  gulong x;

  priv = G_STYLESHEET_GET_PRIVATE (stylesheet);

  engine = cr_sel_eng_new ();

  node = _get_cr_node_from_g_styleable (styleable, &priv->cr_iface);

  status = cr_sel_eng_get_matched_rulesets (engine,
                                            priv->stylesheet,
                                            node,
                                            &stmts_tab,
                                            &length);

  for (x = 0; x < length; x++)
    {
      if (stmts_tab[x]->type == RULESET_STMT)
        {
          CRDeclaration *decl_list = NULL;
          CRDeclaration *decl_property = NULL;
          CRTerm *value = NULL;

          cr_statement_ruleset_get_declarations (stmts_tab[x],
                                                 &decl_list);

          decl_property = cr_declaration_get_by_prop_name (decl_list,
                                                           property_name);

          value = decl_property->value;

          *property = cr_term_to_string (value);
        }
    }
}


static CRNode*
_styleable_get_parent_node (CRNode const *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);
  GStyleable *result;

  result = g_styleable_get_parent_node (styleable);
  return _get_cr_node_from_g_styleable (result, node->iface);
}

static CRNode*
_styleable_first_child (CRNode const *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);
  GStyleable *result;

  result = g_styleable_get_first_child (styleable);
  return _get_cr_node_from_g_styleable (result, node->iface);
}

static CRNode*
_styleable_get_next_sibling (CRNode const *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);
  GStyleable *result;

  result = g_styleable_get_next_sibling (styleable);
  return _get_cr_node_from_g_styleable (result, node->iface);
}

static CRNode*
_styleable_get_previous_sibling (CRNode const *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);
  GStyleable *result;

  result = g_styleable_get_previous_sibling (styleable);
  return _get_cr_node_from_g_styleable (result, node->iface);
}

static char const*
_styleable_get_node_name (CRNode const *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);

  return g_styleable_get_node_name (styleable);
}

static char*
_styleable_get_attribute (CRNode const *node,
                          const char* attribute)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);

  return g_styleable_get_attribute (styleable, attribute);
}

static guint
_styleable_get_children_count (CRNode const *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);

  return g_styleable_get_children_count (styleable);
}

static guint
_styleable_get_index (CRNode const *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);

  return g_styleable_get_attribute (styleable, attribute);
}

static void
_styleable_release (CRNode *node)
{
  GStyleable *styleable = _g_styleable_get_from_cr_node (node);

  g_object_unref (G_OBJECT (styleable));
}


