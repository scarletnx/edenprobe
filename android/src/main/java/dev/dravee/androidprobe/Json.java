// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

/** The report's JSON: one object a line, its keys in the order they were put (no library, so the layer runs it too). */
final class Json {
    private final StringBuilder b = new StringBuilder("{");

    Json put(String key, String value) {
        sep(key);
        quote(b, value);
        return this;
    }

    Json put(String key, long value) {
        sep(key);
        b.append(value);
        return this;
    }

    private void sep(String key) {
        if (b.length() > 1) b.append(',');
        quote(b, key);
        b.append(':');
    }

    @Override public String toString() {
        return b + "}";
    }

    /** RFC 8259's string: quote, backslash and control characters escaped, and a lone surrogate as its \\u code. */
    static String quote(String s) {
        StringBuilder out = new StringBuilder();
        quote(out, s);
        return out.toString();
    }

    private static void quote(StringBuilder out, String s) {
        if (s == null) {
            out.append("null");
            return;
        }
        out.append('"');
        for (int i = 0; i < s.length(); ++i) {
            char c = s.charAt(i);
            boolean pair = Character.isHighSurrogate(c) && i + 1 < s.length() && Character.isLowSurrogate(s.charAt(i + 1));
            if (pair) {
                out.append(c).append(s.charAt(++i));
            } else if (c == '"' || c == '\\') {
                out.append('\\').append(c);
            } else if (c == '\n') {
                out.append("\\n");
            } else if (c == '\t') {
                out.append("\\t");
            } else if (c == '\r') {
                out.append("\\r");
            } else if (c < 0x20 || c == 0x7f || Character.isSurrogate(c)) {
                out.append(String.format(java.util.Locale.ROOT, "\\u%04x", (int) c));
            } else {
                out.append(c);
            }
        }
        out.append('"');
    }

    /** The string value of a key in a line this class wrote, or null: enough to read the report back. */
    static String field(String line, String key) {
        String at = "\"" + key + "\":\"";
        int i = line.indexOf(at);
        if (i < 0) return null;
        StringBuilder v = new StringBuilder();
        for (int k = i + at.length(); k < line.length(); ++k) {
            char c = line.charAt(k);
            if (c == '"') return v.toString();
            if (c == '\\' && k + 1 < line.length()) {
                char e = line.charAt(++k);
                switch (e) {
                    case 'n': v.append('\n'); break;
                    case 't': v.append('\t'); break;
                    case 'r': v.append('\r'); break;
                    case 'u': v.append((char) Integer.parseInt(line.substring(k + 1, k + 5), 16)); k += 4; break;
                    default: v.append(e);
                }
            } else {
                v.append(c);
            }
        }
        return null;
    }
}
