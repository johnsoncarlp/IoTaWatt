#ifndef influxEscape_h
#define influxEscape_h

/*******************************************************************************************************
 * Escaping for InfluxDB names and values (Tracker B50)
 *
 * Measurement, tag and field names come from user configuration and $name substitution, so they can
 * contain characters the InfluxDB syntaxes give a meaning to. Names made of letters, digits and
 * _ $ - . + / come out unchanged in line protocol; queries quote them, which is equivalent for plain
 * names and right for the others (InfluxQL reads an unquoted a.b as retention policy a, measurement b).
 *
 *   lpMeasurement(s)  line protocol measurement: \ before ',' and ' '
 *   lpKey(s)          line protocol tag key/value, field key: \ before ',' '=' and ' '
 *   iqlIdent(s)       InfluxQL identifier: "s", with \ and " escaped
 *   iqlString(s)      InfluxQL string literal: 's', with \ and ' escaped
 *   fluxString(s)     Flux string literal: "s", with \ and " escaped and ${ as \${ (no interpolation)
 *   formEncode(s)     application/x-www-form-urlencoded value (unreserved kept, space as +, rest %XX)
 ******************************************************************************************************/

inline String influxBackslash(const char* s, const char* specials){
  String out;
  for(; s && *s; s++){
    if(strchr(specials, *s)) out += '\\';
    out += *s;
  }
  return out;
}
inline String lpMeasurement(const char* s){ return influxBackslash(s, ", "); }
inline String lpKey(const char* s){ return influxBackslash(s, ",= "); }
inline String iqlIdent(const char* s){ return String('"') + influxBackslash(s, "\\\"") + '"'; }
inline String iqlString(const char* s){ return String('\'') + influxBackslash(s, "\\'") + '\''; }
inline String fluxString(const char* s){
  String out("\"");
  for(; s && *s; s++){
    if(*s == '\\' || *s == '"' || (*s == '$' && s[1] == '{')) out += '\\';
    out += *s;
  }
  return out + '"';
}
inline String formEncode(const char* s){
  static const char hex[] = "0123456789ABCDEF";
  String out;
  for(; s && *s; s++){
    unsigned char c = *s;
    if(isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') out += (char)c;
    else if(c == ' ') out += '+';
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

#endif
