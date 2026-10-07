/* PCRE2-backed regular expression support shared by p5 and p6 modes. */
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "potion.h"

static PN potion_regex_error(Potion *P, int code, PCRE2_SIZE offset) {
  PCRE2_UCHAR message[256];
  int len = pcre2_get_error_message(code, message, sizeof(message));
  if (len < 0)
    return potion_error(P, potion_str(P, "Invalid regular expression"), 0, 0, PN_NIL);
  return potion_error(P,
      potion_str_format(P, "Invalid regular expression at offset %lu: %s",
                        (unsigned long)offset, (char *)message),
      0, 0, PN_NIL);
}

static pcre2_code *potion_regex_compile(Potion *P, PN pattern, PN *error) {
  int code;
  PCRE2_SIZE offset;
  pcre2_code *regex;

  if (!PN_IS_STR(pattern)) {
    *error = potion_type_error(P, pattern);
    return NULL;
  }
  regex = pcre2_compile((PCRE2_SPTR)PN_STR_PTR(pattern), PN_STR_LEN(pattern),
                        PCRE2_UTF | PCRE2_UCP, &code, &offset, NULL);
  if (regex == NULL)
    *error = potion_regex_error(P, code, offset);
  return regex;
}

PN potion_regex_match(Potion *P, PN cl, PN subject, PN pattern) {
  PN error = PN_NIL;
  pcre2_code *regex;
  pcre2_match_data *data;
  int rc;

  if (!PN_IS_STR(subject))
    return potion_type_error(P, subject);
  regex = potion_regex_compile(P, pattern, &error);
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
  pcre2_match_data_free(data);
  pcre2_code_free(regex);
  return rc >= 0 ? PN_TRUE : PN_FALSE;
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
  regex = potion_regex_compile(P, pattern, &error);
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
  }
  pcre2_match_data_free(data);
  pcre2_code_free(regex);
  return captures;
}

void potion_regex_init(Potion *P) {
  PN str_vt = PN_VTABLE(PN_TSTRING);
  potion_method(str_vt, "match", potion_regex_match, "pattern=S");
  potion_method(str_vt, "captures", potion_regex_captures, "pattern=S");
}
