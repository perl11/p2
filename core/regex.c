/* PCRE2-backed regular expression support shared by p5 and p6 modes. */
#define PCRE2_CODE_UNIT_WIDTH 8
#define PCRE2_STATIC
#include <pcre2.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "potion.h"
#include "table.h"

typedef struct {
  pcre2_code *code;
  uint32_t options;
} PNRegex;
static PNType regex_type;


static PN potion_regex_error(Potion *P, int code, PCRE2_SIZE offset) {
  PCRE2_UCHAR message[256];
  int len = pcre2_get_error_message(code, message, sizeof(message));
  if (len < 0)
    return potion_error(P, potion_str(P, "Invalid regular expression"),
                        0, 0, PN_NIL);
  return potion_error(P,
      potion_str_format(P, "Invalid regular expression at offset %lu: %s",
                        (unsigned long)offset, (char *)message),
      0, 0, PN_NIL);
}

static PN potion_regex_runtime_error(Potion *P, int code) {
  PCRE2_UCHAR message[256];
  int len = pcre2_get_error_message(code, message, sizeof(message));
  return potion_error(P,
      len < 0
        ? potion_str(P, "Regular expression operation failed")
        : potion_str_format(P, "Regular expression operation failed: %s",
                            (char *)message),
      0, 0, PN_NIL);
}

static pcre2_code *potion_regex_compile_code(Potion *P, PN pattern,
                                              uint32_t options, PN *error) {
  int code;
  PCRE2_SIZE offset;
  pcre2_code *regex;

  if (!PN_IS_STR(pattern)) {
    *error = potion_type_error(P, pattern);
    return NULL;
  }
  regex = pcre2_compile((PCRE2_SPTR)PN_STR_PTR(pattern), PN_STR_LEN(pattern),
                        options | PCRE2_UTF | PCRE2_UCP,
                        &code, &offset, NULL);
  if (regex == NULL)
    *error = potion_regex_error(P, code, offset);
  return regex;
}

/* Perl's $&, $`, $' and $1..$n: a successful match publishes $&, $` and $' as
 * lobby globals and all groups as ONE tuple global "@^M" (index N = $N, 0 = $&);
 * the p5 grammar compiles $N to an element read of it, so any number of groups
 * works without creating globals at runtime. A failed match leaves them
 * untouched, like Perl. */
static void potion_regex_publish(Potion *P, PN subject, pcre2_match_data *data,
                                 int rc) {
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(data);
  /* the GC moves objects: copy the subject text before allocating anything */
  size_t len = PN_STR_LEN(subject);
  char *text = malloc(len + 1);
  long i, n = rc > 0 ? rc : 1;
  PN val, groups;

  if (text == NULL)
    return;
  memcpy(text, PN_STR_PTR(subject), len);

  groups = PN_TUP0();
  for (i = 0; i < n; i++) {
    if (ovector[2 * i] != PCRE2_UNSET)
      val = potion_str2(P, text + ovector[2 * i],
                        ovector[2 * i + 1] - ovector[2 * i]);
    else
      val = PN_NIL;
    groups = PN_PUSH(groups, val);
  }
  potion_define_global(P, potion_str(P, "@^M"), groups);
  potion_define_global(P, potion_str(P, "$&"), PN_TUPLE_AT(groups, 0));
  potion_define_global(P, potion_str(P, "$`"),
                       potion_str2(P, text, ovector[0]));
  potion_define_global(P, potion_str(P, "$'"),
                       potion_str2(P, text + ovector[1], len - ovector[1]));
  free(text);
}

static PN potion_regex_run_match(Potion *P, pcre2_code *regex, PN subject) {
  pcre2_match_data *data;
  int rc;

  if (!PN_IS_STR(subject))
    return potion_type_error(P, subject);
  data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (data == NULL)
    return potion_error(P, potion_str(P, "Unable to allocate regex match data"),
                        0, 0, PN_NIL);
  rc = pcre2_match(regex, (PCRE2_SPTR)PN_STR_PTR(subject), PN_STR_LEN(subject),
                   0, 0, data, NULL);
  if (rc >= 0)
    potion_regex_publish(P, subject, data, rc);
  pcre2_match_data_free(data);
  if (rc >= 0)
    return PN_TRUE;
  if (rc == PCRE2_ERROR_NOMATCH)
    return PN_FALSE;
  return potion_regex_runtime_error(P, rc);
}

static PNRegex *potion_compiled_regex(Potion *P, PN self, PN *error) {
  PN storage;
  PNRegex *regex;
  if (!PN_IS_PTR(self) || PN_VTYPE(self) != regex_type) {
    *error = potion_type_error(P, self);
    return NULL;
  }
  storage = ((struct PNObject *)potion_fwd(self))->ivars[0];
  if (!PN_IS_PTR(storage) || PN_VTYPE(storage) != PN_TUSER) {
    *error = potion_error(P, potion_str(P, "Invalid compiled regular expression"),
                          0, 0, PN_NIL);
    return NULL;
  }
  regex = (PNRegex *)PN_DATA(potion_fwd(storage));
  if (regex->code == NULL) {
    *error = potion_error(P, potion_str(P, "Regular expression is closed"),
                          0, 0, PN_NIL);
    return NULL;
  }
  return regex;
}

/* Regex.compile(pattern, options) returns a persistent compiled expression.
 * options is the raw PCRE2 compile-option bitmask; UTF and UCP remain enabled
 * for compatibility with String.match and String.captures. */
static PN potion_compiled_regex_compile(Potion *P, PN cl, PN self,
                                         PN pattern, PN options) {
  PN object, storage;
  PNRegex *compiled;
  PN error = PN_NIL;
  double raw;
  uint32_t flags;
  pcre2_code *code;

  PN_CHECK_NUM(options);
  raw = PN_DBL(options);
  if (raw < 0 || raw > UINT32_MAX || raw != (double)(uint32_t)raw)
    return potion_error(P, potion_str(P, "Regex options must be a uint32"),
                        0, 0, PN_NIL);
  flags = (uint32_t)raw;
  code = potion_regex_compile_code(P, pattern, flags, &error);
  if (code == NULL)
    return error;

  storage = (PN)potion_data_alloc(P, sizeof(PNRegex));
  compiled = (PNRegex *)PN_DATA(storage);
  compiled->code = code;
  compiled->options = flags;
  object = (PN)PN_ALLOC_N(regex_type, struct PNObject, sizeof(PN));
  storage = potion_fwd(storage);
  ((struct PNObject *)object)->ivars[0] = storage;
  PN_TOUCH(object);
  return object;
}

static PN potion_compiled_regex_match(Potion *P, PN cl, PN self, PN subject) {
  PN error = PN_NIL;
  PNRegex *regex = potion_compiled_regex(P, self, &error);
  return regex == NULL ? error : potion_regex_run_match(P, regex->code, subject);
}

/* Replace the first (or with PCRE2_SUBSTITUTE_GLOBAL every) match. Returns
 * subject itself when nothing matched. Replacement syntax is PCRE2's: $1,
 * ${1}, $0 for the whole match. */
static PN potion_regex_substitute(Potion *P, pcre2_code *code, PN subject,
                                  PN replacement, uint32_t flags) {
  pcre2_match_data *data;
  PCRE2_UCHAR *output;
  PCRE2_SIZE capacity, length;
  int rc;

  if (!PN_IS_STR(subject))
    return potion_type_error(P, subject);
  if (!PN_IS_STR(replacement))
    return potion_type_error(P, replacement);

  data = pcre2_match_data_create_from_pattern(code, NULL);
  if (data == NULL)
    return potion_error(P, potion_str(P, "Unable to allocate regex match data"),
                        0, 0, PN_NIL);
  capacity = PN_STR_LEN(subject) + PN_STR_LEN(replacement) + 1;
  output = malloc(capacity);
  if (output == NULL) {
    pcre2_match_data_free(data);
    return potion_error(P, potion_str(P, "Unable to allocate regex output"),
                        0, 0, PN_NIL);
  }

  for (;;) {
    length = capacity;
    rc = pcre2_substitute(code,
        (PCRE2_SPTR)PN_STR_PTR(subject), PN_STR_LEN(subject), 0,
        flags | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, data, NULL,
        (PCRE2_SPTR)PN_STR_PTR(replacement), PN_STR_LEN(replacement),
        output, &length);
    if (rc != PCRE2_ERROR_NOMEMORY)
      break;
    capacity = length + 1;
    {
      PCRE2_UCHAR *larger = realloc(output, capacity);
      if (larger == NULL) {
        free(output);
        pcre2_match_data_free(data);
        return potion_error(P, potion_str(P, "Unable to grow regex output"),
                            0, 0, PN_NIL);
      }
      output = larger;
    }
  }

  pcre2_match_data_free(data);
  if (rc == PCRE2_ERROR_NOMATCH) {
    free(output);
    return subject;
  }
  if (rc < 0) {
    free(output);
    return potion_regex_runtime_error(P, rc);
  }
  {
    PN result = potion_str2(P, (char *)output, length);
    free(output);
    return result;
  }
}


static PN potion_compiled_regex_replace(Potion *P, PN cl, PN self,
                                         PN subject, PN replacement) {
  PN error = PN_NIL;
  PNRegex *regex = potion_compiled_regex(P, self, &error);
  if (regex == NULL)
    return error;
  return potion_regex_substitute(P, regex->code, subject, replacement, 0);
}

/* String#subst(pattern, replacement, global): backs p5 s/pattern/repl/[g]. */
static PN potion_regex_subst(Potion *P, PN cl, PN subject, PN pattern,
                             PN replacement, PN global) {
  PN error = PN_NIL, result;
  pcre2_code *code = potion_regex_compile_code(P, pattern, 0, &error);
  if (code == NULL)
    return error;
  result = potion_regex_substitute(P, code, subject, replacement,
                                   PN_TEST(global) ? PCRE2_SUBSTITUTE_GLOBAL : 0);
  pcre2_code_free(code);
  return result;
}

static PN potion_compiled_regex_options(Potion *P, PN cl, PN self) {
  PN error = PN_NIL;
  PNRegex *regex = potion_compiled_regex(P, self, &error);
  return regex == NULL ? error : PN_NUM(regex->options);
}

/* The GC has no native-resource finalizers. close permits deterministic
 * release for applications that compile an unbounded number of patterns. */
static PN potion_compiled_regex_close(Potion *P, PN cl, PN self) {
  PN error = PN_NIL;
  PNRegex *regex = potion_compiled_regex(P, self, &error);
  if (regex == NULL)
    return error;
  pcre2_code_free(regex->code);
  regex->code = NULL;
  return self;
}

PN potion_regex_match(Potion *P, PN cl, PN subject, PN pattern) {
  PN error = PN_NIL;
  pcre2_code *regex = potion_regex_compile_code(P, pattern, 0, &error);
  PN result;
  if (regex == NULL)
    return error;
  result = potion_regex_run_match(P, regex, subject);
  pcre2_code_free(regex);
  return result;
}

PN potion_regex_captures(Potion *P, PN cl, PN subject, PN pattern) {
  PN error = PN_NIL;
  PN captures = PN_TUP0();
  pcre2_code *regex;
  pcre2_match_data *data;
  PCRE2_SIZE *ovector;
  int rc, i;

  if (!PN_IS_STR(subject))
    return potion_type_error(P, subject);
  regex = potion_regex_compile_code(P, pattern, 0, &error);
  if (regex == NULL)
    return error;
  data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (data == NULL) {
    pcre2_code_free(regex);
    return potion_error(P, potion_str(P, "Unable to allocate regex match data"),
                        0, 0, PN_NIL);
  }
  rc = pcre2_match(regex, (PCRE2_SPTR)PN_STR_PTR(subject), PN_STR_LEN(subject),
                   0, 0, data, NULL);
  if (rc >= 0) {
    ovector = pcre2_get_ovector_pointer(data);
    for (i = 0; i < rc; i++) {
      if (ovector[2 * i] == PCRE2_UNSET)
        captures = PN_PUSH(captures, PN_NIL);
      else
        captures = PN_PUSH(captures,
            potion_str2(P, PN_STR_PTR(subject) + ovector[2 * i],
                        ovector[2 * i + 1] - ovector[2 * i]));
    }
  } else if (rc != PCRE2_ERROR_NOMATCH) {
    captures = potion_regex_runtime_error(P, rc);
  }
  pcre2_match_data_free(data);
  pcre2_code_free(regex);
  return captures;
}

void potion_regex_init(Potion *P) {
  { /* pre-create the match variables so a match only updates existing keys */
    potion_define_global(P, potion_str(P, "$&"), PN_NIL);
    potion_define_global(P, potion_str(P, "$`"), PN_NIL);
    potion_define_global(P, potion_str(P, "$'"), PN_NIL);
    potion_define_global(P, potion_str(P, "@^M"), PN_TUP0());
  }
  PN str_vt = PN_VTABLE(PN_TSTRING);
  PN regex_vt = potion_class(P, PN_NIL, PN_VTABLE(PN_TOBJECT),
                             PN_TUP(PN_STR("_data")));
  regex_type = potion_class_type(P, regex_vt);

  potion_define_global(P, potion_str(P, "Regex"), regex_vt);
  potion_class_method(regex_vt, "compile", potion_compiled_regex_compile,
                      "pattern=S,options=N");
  potion_method(regex_vt, "match", potion_compiled_regex_match, "subject=S");
  potion_method(regex_vt, "replace", potion_compiled_regex_replace,
                "subject=S,replacement=S");
  potion_method(regex_vt, "options", potion_compiled_regex_options, 0);
  potion_method(regex_vt, "close", potion_compiled_regex_close, 0);

  potion_method(str_vt, "match", potion_regex_match, "pattern=S");
  potion_method(str_vt, "subst", potion_regex_subst,
                "pattern=S,replacement=S,global=o");
  potion_method(str_vt, "captures", potion_regex_captures, "pattern=S");
}
