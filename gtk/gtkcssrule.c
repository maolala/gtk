/*
 * Copyright © 2016 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */

#include "config.h"

#include "gtkcssruleprivate.h"

#include "gtkcssdefinecolorruleprivate.h"
#include "gtkcssimportruleprivate.h"
#include "gtkcsskeyframesruleprivate.h"
#include "gtkcssstylesheetprivate.h"
#include "gtkintl.h"
#include "gtkprivate.h"

enum {
  PROP_0,
  PROP_CSS_TEXT,
  PROP_PARENT_RULE,
  PROP_PARENT_STYLESHEET,
  NUM_PROPERTIES
};

typedef struct _GtkCssRulePrivate GtkCssRulePrivate;
struct _GtkCssRulePrivate {
  GtkCssRule *parent_rule;
  GtkCssStyleSheet *parent_style_sheet;
};

typedef struct _GtkCssTokenSourceAt GtkCssTokenSourceAt;
struct _GtkCssTokenSourceAt {
  GtkCssTokenSource parent;
  GtkCssTokenSource *source;
  GSList *blocks;
  guint done :1;
};

static GParamSpec *rule_props[NUM_PROPERTIES] = { NULL, };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GtkCssRule, gtk_css_rule, G_TYPE_OBJECT)

static void
gtk_css_token_source_at_finalize (GtkCssTokenSource *source)
{
  GtkCssTokenSourceAt *at = (GtkCssTokenSourceAt *) source;

  g_slist_free (at->blocks);

  gtk_css_token_source_unref (at->source);
}

static void
gtk_css_token_source_at_consume_token (GtkCssTokenSource *source,
                                       GObject           *consumer)
{
  GtkCssTokenSourceAt *at = (GtkCssTokenSourceAt *) source;
  const GtkCssToken *token;

  if (at->done)
    return;

  token = gtk_css_token_source_peek_token (at->source);
  switch (token->type)
    {
    case GTK_CSS_TOKEN_FUNCTION:
    case GTK_CSS_TOKEN_OPEN_PARENS:
      at->blocks = g_slist_prepend (at->blocks, GUINT_TO_POINTER (GTK_CSS_TOKEN_CLOSE_PARENS));
      break;
    case GTK_CSS_TOKEN_OPEN_SQUARE:
      at->blocks = g_slist_prepend (at->blocks, GUINT_TO_POINTER (GTK_CSS_TOKEN_CLOSE_SQUARE));
      break;
    case GTK_CSS_TOKEN_OPEN_CURLY:
      at->blocks = g_slist_prepend (at->blocks, GUINT_TO_POINTER (GTK_CSS_TOKEN_CLOSE_CURLY));
      break;
    case GTK_CSS_TOKEN_CLOSE_PARENS:
    case GTK_CSS_TOKEN_CLOSE_SQUARE:
    case GTK_CSS_TOKEN_CLOSE_CURLY:
      if (at->blocks && GPOINTER_TO_UINT (at->blocks->data) == token->type)
        {
          at->blocks = g_slist_remove (at->blocks, at->blocks->data);
          if (token->type == GTK_CSS_TOKEN_CLOSE_CURLY && at->blocks == NULL)
            at->done = TRUE;
        }
      break;
    case GTK_CSS_TOKEN_SEMICOLON:
      if (at->blocks == NULL)
        at->done = TRUE;
      break;
    default:
      break;
    }

  gtk_css_token_source_consume_token_as (at->source, consumer);
}

const GtkCssToken *
gtk_css_token_source_at_peek_token (GtkCssTokenSource *source)
{
  GtkCssTokenSourceAt *at = (GtkCssTokenSourceAt *) source;
  static GtkCssToken eof_token = { GTK_CSS_TOKEN_EOF };

  if (at->done)
    return &eof_token;

  return gtk_css_token_source_peek_token (at->source);
}

static void
gtk_css_token_source_at_error (GtkCssTokenSource *source,
                               const GError      *error)
{
  GtkCssTokenSourceAt *at = (GtkCssTokenSourceAt *) source;

  gtk_css_token_source_emit_error (at->source, error);
}

static GFile *
gtk_css_token_source_at_get_location (GtkCssTokenSource *source)
{
  GtkCssTokenSourceAt *at = (GtkCssTokenSourceAt *) source;

  return gtk_css_token_source_get_location (at->source);
}

static const GtkCssTokenSourceClass GTK_CSS_TOKEN_SOURCE_AT = {
  gtk_css_token_source_at_finalize,
  gtk_css_token_source_at_consume_token,
  gtk_css_token_source_at_peek_token,
  gtk_css_token_source_at_error,
  gtk_css_token_source_at_get_location,
};

static GtkCssTokenSource *
gtk_css_token_source_new_at (GtkCssTokenSource *source)
{
  GtkCssTokenSourceAt *at = gtk_css_token_source_new (GtkCssTokenSourceAt, &GTK_CSS_TOKEN_SOURCE_AT);

  at->source = gtk_css_token_source_ref (source);
  gtk_css_token_source_set_consumer (&at->parent,
                                     gtk_css_token_source_get_consumer (source));

  return &at->parent;
}

static void
gtk_css_rule_set_property (GObject      *gobject,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GtkCssRule *rule = GTK_CSS_RULE (gobject);
  GtkCssRulePrivate *priv = gtk_css_rule_get_instance_private (rule);

  switch (prop_id)
    {
    case PROP_PARENT_RULE:
      priv->parent_rule = g_value_dup_object (value);
      break;

    case PROP_PARENT_STYLESHEET:
      priv->parent_style_sheet = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
gtk_css_rule_get_property (GObject    *gobject,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  GtkCssRule *rule = GTK_CSS_RULE (gobject);
  GtkCssRulePrivate *priv = gtk_css_rule_get_instance_private (rule);

  switch (prop_id)
    {
    case PROP_CSS_TEXT:
      g_value_take_string (value, gtk_css_rule_get_css_text (rule));
      break;

    case PROP_PARENT_RULE:
      g_value_set_object (value, priv->parent_rule);
      break;

    case PROP_PARENT_STYLESHEET:
      g_value_set_object (value, priv->parent_style_sheet);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
    }
}

static void
gtk_css_rule_class_init (GtkCssRuleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gtk_css_rule_set_property;
  object_class->get_property = gtk_css_rule_get_property;

  rule_props[PROP_CSS_TEXT] =
      g_param_spec_string ("css-text",
                           P_("CSS text"),
                           P_("Conversion this rule to text"),
                           NULL,
                           GTK_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);
  rule_props[PROP_PARENT_RULE] =
      g_param_spec_object ("parent-rule",
                           P_("parent rule"),
                           P_("The parent CSS rule if it exists"),
                           GTK_TYPE_CSS_RULE,
                           GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_EXPLICIT_NOTIFY);
  rule_props[PROP_PARENT_STYLESHEET] =
      g_param_spec_object ("parent-stylesheet",
                           P_("parent style sheet"),
                           P_("The parent style sheet that contains this rule"),
                           GTK_TYPE_CSS_STYLE_SHEET,
                           GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, NUM_PROPERTIES, rule_props);

}

static void
gtk_css_rule_init (GtkCssRule *rule)
{
}

GtkCssRule *
gtk_css_rule_new_from_at_rule (GtkCssTokenSource *source,
                               GtkCssRule        *parent_rule,
                               GtkCssStyleSheet  *parent_style_sheet)
{
  GtkCssTokenSource *at_source;
  const GtkCssToken *token;
  GtkCssRule *rule;

  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (parent_rule == NULL || GTK_IS_CSS_RULE (parent_rule), NULL);
  g_return_val_if_fail (GTK_IS_CSS_STYLE_SHEET (parent_style_sheet), NULL);

  at_source = gtk_css_token_source_new_at (source);

  token = gtk_css_token_source_get_token (at_source);
  if (token->type != GTK_CSS_TOKEN_AT_KEYWORD)
    {
      gtk_css_token_source_error (at_source, "Expected an '@'");
      gtk_css_token_source_consume_all (at_source);
      gtk_css_token_source_unref (at_source);
      return NULL;
    }

  if (g_ascii_strcasecmp (token->string.string, "import") == 0)
    {
      rule = gtk_css_import_rule_new_parse (at_source, parent_rule, parent_style_sheet);
    }
  else if (g_ascii_strcasecmp (token->string.string, "define-color") == 0)
    {
      rule = gtk_css_define_color_rule_new_parse (at_source, parent_rule, parent_style_sheet);
    }
  else if (g_ascii_strcasecmp (token->string.string, "keyframes") == 0)
    {
      rule = gtk_css_keyframes_rule_new_parse (at_source, parent_rule, parent_style_sheet);
    }
  else
    {
      gtk_css_token_source_unknown (at_source, "Unknown rule @%s", token->string.string);
      gtk_css_token_source_consume_all (at_source);
      rule = NULL;
    }

  token = gtk_css_token_source_get_token (at_source);
  if (rule != NULL && !gtk_css_token_is (token, GTK_CSS_TOKEN_EOF))
    {
      gtk_css_token_source_unknown (at_source, "Junk at end of @-rule");
      gtk_css_token_source_consume_all (at_source);
      g_object_unref (rule);
      rule = NULL;
    }
  gtk_css_token_source_unref (at_source);

  return rule;
}

void
gtk_css_rule_print_css_text (GtkCssRule *rule,
                             GString    *string)
{
  GtkCssRuleClass *klass;

  g_return_if_fail (GTK_IS_CSS_RULE (rule));
  g_return_if_fail (string != NULL);

  klass = GTK_CSS_RULE_GET_CLASS (rule);

  klass->get_css_text (rule, string);
}

char *
gtk_css_rule_get_css_text (GtkCssRule *rule)
{
  GString *string;

  g_return_val_if_fail (GTK_IS_CSS_RULE (rule), NULL);

  string = g_string_new (NULL);

  gtk_css_rule_print_css_text (rule, string);

  return g_string_free (string, FALSE);
}

GtkCssRule *
gtk_css_rule_get_parent_rule (GtkCssRule *rule)
{
  GtkCssRulePrivate *priv;

  g_return_val_if_fail (GTK_IS_CSS_RULE (rule), NULL);

  priv = gtk_css_rule_get_instance_private (rule);

  return priv->parent_rule;
}

GtkCssStyleSheet *
gtk_css_rule_get_parent_style_sheet (GtkCssRule *rule)
{
  GtkCssRulePrivate *priv;

  g_return_val_if_fail (GTK_IS_CSS_RULE (rule), NULL);

  priv = gtk_css_rule_get_instance_private (rule);

  return priv->parent_style_sheet;
}