// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

import android.content.Context;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.content.res.XmlResourceParser;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Locale;
import org.xmlpull.v1.XmlPullParser;

/** The APK's resources: the table (resources.arsc) and the binary XML that aapt2 compiled, and the assets. */
final class ResourceCases {
    private ResourceCases() {}

    static String read(InputStream in) throws IOException {
        try (InputStream i = in) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            for (int n; (n = i.read(buf)) > 0;) out.write(buf, 0, n);
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    /** A string with format arguments, and one with aapt2's escapes (\t, \", \', \\u0040). */
    static final class Strings implements Case {
        @Override public Outcome run(Env env) {
            Resources r = env.activity.getResources();
            return Outcome.check("Hello probe, 3 cases|tab\tquote\" apos' at@ end|alpha beta|3 -1",
                r.getString(R.string.greeting, "probe", 3) + "|" + r.getString(R.string.escaped) + "|"
                    + String.join(" ", r.getStringArray(R.array.words)) + "|" + r.getIntArray(R.array.numbers)[0] + " "
                    + r.getIntArray(R.array.numbers)[1]);
        }
    }

    /** English plurals through a configuration context of en-US: 1 is one, 0 and 2 are other. */
    static final class Plurals implements Case {
        @Override public Outcome run(Env env) {
            Configuration c = new Configuration(env.activity.getResources().getConfiguration());
            c.setLocale(Locale.US);
            Context en = env.activity.createConfigurationContext(c);
            Resources r = en.getResources();
            return Outcome.check("0 cases|1 case|2 cases", r.getQuantityString(R.plurals.cases, 0, 0) + "|"
                + r.getQuantityString(R.plurals.cases, 1, 1) + "|" + r.getQuantityString(R.plurals.cases, 2, 2));
        }
    }

    /** The table's typed values: a #rgb color's type and data, dimensions in dp and px, integer, bool, fraction. */
    static final class TypedValues implements Case {
        @Override public Outcome run(Env env) {
            Resources r = env.activity.getResources();
            TypedValue tv = new TypedValue();
            r.getValue(R.color.probe_red, tv, true);
            float dp = r.getDimension(R.dimen.d16dp) / r.getDisplayMetrics().density;
            String got = String.format(Locale.ROOT, "color type 0x%x data 0x%08x|16dp is %.3f dp|7px is %d px|%d|%b|%.2f",
                tv.type, tv.data, dp, r.getDimensionPixelSize(R.dimen.d7px), r.getInteger(R.integer.answer),
                r.getBoolean(R.bool.yes), r.getFraction(R.fraction.half, 1, 1));
            return Outcome.check("color type 0x1d data 0xffff0000|16dp is 16.000 dp|7px is 7 px|42|true|0.50", got);
        }
    }

    /** A binary XML resource walked with the parser: tags, text, attributes by name, a reference as "@" and its id. */
    static final class BinaryXml implements Case {
        @Override public Outcome run(Env env) throws Exception {
            StringBuilder got = new StringBuilder();
            try (XmlResourceParser p = env.activity.getResources().getXml(R.xml.probe)) {
                for (int e = p.getEventType(); e != XmlPullParser.END_DOCUMENT; e = p.next()) {
                    if (e == XmlPullParser.START_TAG) {
                        got.append('<').append(p.getName()).append(" n=").append(p.getAttributeCount());
                        if (p.getName().equals("root")) {
                            got.append(" a=").append(p.getAttributeValue(null, "a"))
                                .append(" b=").append(p.getAttributeValue(null, "b"))
                                .append(" b-ref=").append(p.getAttributeResourceValue(null, "b", 0) == R.string.app_name)
                                .append(" id=").append(p.getAttributeValue("http://schemas.android.com/apk/res/android", "id"));
                        } else {
                            got.append(" c=").append(p.getAttributeBooleanValue(null, "c", false));
                        }
                        got.append('>');
                    } else if (e == XmlPullParser.END_TAG) {
                        got.append("</").append(p.getName()).append('>');
                    } else if (e == XmlPullParser.TEXT) {
                        got.append(p.getText());
                    }
                }
            }
            String want = "<root n=3 a=1 b=@" + R.string.app_name + " b-ref=true id=@" + R.id.probe_xml_root
                + "><item n=1 c=true>text</item></root>";
            return Outcome.check(want, got.toString());
        }
    }

    /** A layout inflated: its view classes, the text from a string resource, a weight and a visibility. */
    static final class InflateLayout implements Case {
        @Override public Outcome run(Env env) throws Exception {
            String got = env.onUi(() -> {
                View v = LayoutInflater.from(env.activity).inflate(R.layout.probe_layout, null);
                LinearLayout root = (LinearLayout) v;
                StringBuilder b = new StringBuilder(v.getClass().getSimpleName()).append('[');
                for (int i = 0; i < root.getChildCount(); ++i)
                    b.append(i > 0 ? "," : "").append(root.getChildAt(i).getClass().getSimpleName());
                b.append("] orientation ").append(root.getOrientation()).append('|');
                TextView text = v.findViewById(R.id.probe_text);
                b.append(text == null ? "no view with the id probe_text" : text.getText()).append('|');
                View weighted = v.findViewById(R.id.probe_view);
                if (weighted == null) return b.append("no view with the id probe_view").toString();
                LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) weighted.getLayoutParams();
                return b.append(lp.width).append(' ').append(lp.weight).append(' ').append(weighted.getVisibility()).toString();
            });
            return Outcome.check("LinearLayout[TextView,View] orientation 1|edenprobe|10 2.0 4", got);
        }
    }

    /** A raw resource and the assets: the bytes, a folder's list, and openFd of a compressed asset refused. */
    static final class RawAndAssets implements Case {
        @Override public Outcome run(Env env) throws Exception {
            AssetManager am = env.activity.getAssets();
            String fd;
            try {
                am.openFd("probe/one.txt").close();
                fd = "opened";
            } catch (FileNotFoundException e) {
                fd = "FileNotFoundException";
            }
            String[] list = am.list("probe");
            Arrays.sort(list);
            String got = read(env.activity.getResources().openRawResource(R.raw.probe_data)).trim() + "|" + Arrays.toString(list)
                + "|" + read(am.open("probe/two.txt")).trim() + "|openFd " + fd;
            return Outcome.check("raw bytes|[one.txt, two.txt]|two|openFd FileNotFoundException", got);
        }
    }
}
