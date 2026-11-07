// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.pdf.PdfDocument;
import android.media.MediaPlayer;
import android.net.LinkProperties;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Parcel;
import android.util.AtomicFile;
import android.util.Log;
import android.view.Gravity;
import android.widget.PopupWindow;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.Inet4Address;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.zip.Inflater;
import org.json.JSONObject;

/** The framework's Java API: a signature and a behaviour each, the review's findings on #1073 among them. */
final class FrameworkCases {
    private FrameworkCases() {}

    static String files(File dir) {
        String[] names = dir.list();
        if (names == null) return "[unreadable]";
        Arrays.sort(names);
        return Arrays.toString(names);
    }

    static void write(File f, String text) throws IOException {
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    /** The SDK's descriptor is ()Ljava/net/Inet4Address;: a framework that returns InetAddress has no such method. */
    static final class LinkPropertiesDhcp implements Case {
        @Override public Outcome run(Env env) {
            if (Build.VERSION.SDK_INT < 30) return Outcome.skip("needs API 30, this is API " + Build.VERSION.SDK_INT);
            Inet4Address a = new LinkProperties().getDhcpServerAddress();
            return Outcome.check("null", String.valueOf(a));
        }
    }

    /** MediaPlayer opens a path with FileInputStream first: a missing file is FileNotFoundException. */
    static final class MediaPlayerMissingFile implements Case {
        @Override public Outcome run(Env env) throws Exception {
            MediaPlayer mp = new MediaPlayer();
            String path = new File(env.scratch("media"), "missing.ogg").getPath();
            try {
                mp.setDataSource(path);
                return Outcome.check("java.io.FileNotFoundException", "returned");
            } catch (Exception e) {
                Outcome o = Outcome.check("java.io.FileNotFoundException", e.getClass().getName());
                return o.passed ? o : Outcome.fail(o.detail + "; " + Outcome.describe(e));
            } finally {
                mp.release();
            }
        }
    }

    /** writeTo writes what the page's canvas drew: the rectangle (re) and the text (Tj or TJ) in its content. */
    static final class PdfDocumentDraws implements Case {
        @Override public Outcome run(Env env) throws Exception {
            PdfDocument doc = new PdfDocument();
            PdfDocument.Page page = doc.startPage(new PdfDocument.PageInfo.Builder(200, 200, 1).create());
            Canvas c = page.getCanvas();
            Paint paint = new Paint();
            paint.setColor(Color.RED);
            c.drawRect(20, 20, 180, 80, paint);
            paint.setTextSize(24);
            c.drawText("edenprobe", 20, 140, paint);
            doc.finishPage(page);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            doc.writeTo(out);
            doc.close();
            byte[] pdf = out.toByteArray();
            String ops = streams(pdf);
            String got = "header " + new String(pdf, 0, Math.min(5, pdf.length), StandardCharsets.ISO_8859_1)
                + ", rectangle " + (ops.matches("(?s).*\\bre\\b.*") ? "yes" : "no")
                + ", text " + (ops.matches("(?s).*\\bT[jJ]\\b.*") ? "yes" : "no");
            return Outcome.check("header %PDF-, rectangle yes, text yes", got);
        }

        /** every stream of the file, inflated where it is Flate (all of Skia's are), as one string */
        static String streams(byte[] pdf) {
            String s = new String(pdf, StandardCharsets.ISO_8859_1);
            StringBuilder all = new StringBuilder();
            for (int i = s.indexOf("stream"); i >= 0; i = s.indexOf("stream", i + 6)) {
                if (i >= 3 && s.startsWith("end", i - 3)) continue;
                int start = i + 6;
                if (s.startsWith("\r\n", start)) start += 2;
                else if (s.startsWith("\n", start)) start += 1;
                int end = s.indexOf("endstream", start);
                if (end < 0) break;
                byte[] raw = Arrays.copyOfRange(pdf, start, end);
                Inflater inf = new Inflater();
                inf.setInput(raw);
                byte[] buf = new byte[65536];
                try {
                    int n = inf.inflate(buf);
                    all.append(n > 0 ? new String(buf, 0, n, StandardCharsets.ISO_8859_1) : new String(raw, StandardCharsets.ISO_8859_1));
                } catch (java.util.zip.DataFormatException e) {
                    all.append(new String(raw, StandardCharsets.ISO_8859_1));
                } finally {
                    inf.end();
                }
                all.append('\n');
                i = end;
            }
            return all.toString();
        }
    }

    /** finishWrite renames the new file over the base: the second write's bytes, and no file but the base. */
    static final class AtomicFileFinish implements Case {
        @Override public Outcome run(Env env) throws Exception {
            File dir = env.scratch("atomic-finish");
            AtomicFile af = new AtomicFile(new File(dir, "data"));
            for (String text : new String[] {"first", "second"}) {
                FileOutputStream out = af.startWrite();
                out.write(text.getBytes(StandardCharsets.UTF_8));
                af.finishWrite(out);
            }
            return Outcome.check("second, files [data]", new String(af.readFully(), StandardCharsets.UTF_8) + ", files " + files(dir));
        }
    }

    /** A legacy backup (data.bak) is the last whole write: openRead puts it back over the base before it reads. */
    static final class AtomicFileLegacyBackup implements Case {
        @Override public Outcome run(Env env) throws Exception {
            File dir = env.scratch("atomic-backup");
            write(new File(dir, "data"), "partial");
            write(new File(dir, "data.bak"), "whole");
            AtomicFile af = new AtomicFile(new File(dir, "data"));
            return Outcome.check("whole, files [data]", new String(af.readFully(), StandardCharsets.UTF_8) + ", files " + files(dir));
        }
    }

    /** A shown popup is a window: its content view gets attached and laid out at the popup's size. */
    static final class PopupWindowShows implements Case {
        @Override public Outcome run(Env env) throws Exception {
            long deadline = System.currentTimeMillis() + Env.UI_TIMEOUT_MS;
            while (env.onUi(() -> env.root.getWindowToken() == null)) {
                if (System.currentTimeMillis() > deadline)
                    return Outcome.fail("the activity's window had no token after " + Env.UI_TIMEOUT_MS + " ms");
                Thread.sleep(50);
            }
            TextView content = env.onUi(() -> {
                TextView t = new TextView(env.activity);
                t.setText("popup");
                return t;
            });
            PopupWindow pw = env.onUi(() -> {
                PopupWindow p = new PopupWindow(content, 120, 60);
                p.showAtLocation(env.root, Gravity.TOP | Gravity.START, 0, 0);
                return p;
            });
            deadline = System.currentTimeMillis() + Env.UI_TIMEOUT_MS;
            while (env.onUi(() -> !content.isAttachedToWindow() || content.getWidth() == 0) && System.currentTimeMillis() < deadline)
                Thread.sleep(50);
            String got = env.onUi(() -> "showing " + pw.isShowing() + ", attached " + content.isAttachedToWindow()
                + ", size " + content.getWidth() + "x" + content.getHeight());
            got += env.onUi(() -> {
                pw.dismiss();
                return ", after dismiss showing " + pw.isShowing();
            });
            return Outcome.check("showing true, attached true, size 120x60, after dismiss showing false", got);
        }
    }

    /** A Handler runs the message at the front first, then by time: an immediate one before a delayed one. */
    static final class HandlerOrder implements Case {
        @Override public Outcome run(Env env) throws Exception {
            HandlerThread t = new HandlerThread("edenprobe-order");
            t.start();
            try {
                Handler h = new Handler(t.getLooper());
                CountDownLatch gate = new CountDownLatch(1), done = new CountDownLatch(3);
                StringBuffer order = new StringBuffer();
                h.post(() -> {
                    try {
                        gate.await();
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                });
                h.postDelayed(() -> { order.append("delayed "); done.countDown(); }, 30);
                h.post(() -> { order.append("posted "); done.countDown(); });
                h.postAtFrontOfQueue(() -> { order.append("front "); done.countDown(); });
                gate.countDown();
                if (!done.await(Env.UI_TIMEOUT_MS, TimeUnit.MILLISECONDS)) order.append("(timed out)");
                return Outcome.check("front posted delayed", order.toString().trim());
            } finally {
                t.quitSafely();
            }
        }
    }

    /** Parcel's layout: an int, a string as its length and UTF-16 with a terminator padded to 4, then a long. */
    static final class ParcelLayout implements Case {
        @Override public Outcome run(Env env) {
            Parcel p = Parcel.obtain();
            try {
                p.writeInt(7);
                p.writeString("abc");
                p.writeLong(-2);
                StringBuilder hex = new StringBuilder();
                for (byte b : p.marshall()) hex.append(String.format(Locale.ROOT, "%02x", b & 0xff));
                p.setDataPosition(0);
                String back = p.readInt() + " " + p.readString() + " " + p.readLong();
                return Outcome.check("24 bytes 07000000030000006100620063000000feffffffffffffff, read 7 abc -2",
                    p.dataSize() + " bytes " + hex + ", read " + back);
            } finally {
                p.recycle();
            }
        }
    }

    static final class UriParts implements Case {
        @Override public Outcome run(Env env) {
            Uri u = Uri.parse("https://user@example.org:8080/a%20b/c?x=1&y=%C3%A9#frag");
            String got = u.getScheme() + "|" + u.getAuthority() + "|" + u.getHost() + "|" + u.getPort() + "|" + u.getPath()
                + "|" + u.getEncodedPath() + "|" + u.getQueryParameter("y") + "|" + u.getFragment() + "|" + u.getLastPathSegment();
            return Outcome.check("https|user@example.org:8080|example.org|8080|/a b/c|/a%20b/c|é|frag|c", got);
        }
    }

    /** commit writes the file shared_prefs/NAME.xml in the app's data folder, in Android's XML. */
    static final class SharedPreferencesFile implements Case {
        @Override public Outcome run(Env env) throws Exception {
            SharedPreferences sp = env.activity.getSharedPreferences("edenprobe", Context.MODE_PRIVATE);
            boolean ok = sp.edit().putInt("i", 7).putString("s", "x\"<y").commit();
            File f = new File(env.activity.getFilesDir().getParentFile(), "shared_prefs/edenprobe.xml");
            String xml = f.exists() ? new String(java.nio.file.Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8) : "";
            String got = "commit " + ok + ", file " + f.exists() + ", int " + xml.contains("<int name=\"i\" value=\"7\" />")
                + ", string " + xml.contains("<string name=\"s\">x&quot;&lt;y</string>") + ", read " + sp.getInt("i", 0) + " " + sp.getString("s", null);
            return Outcome.check("commit true, file true, int true, string true, read 7 x\"<y", got);
        }
    }

    static final class SqliteGroupBy implements Case {
        @Override public Outcome run(Env env) {
            SQLiteDatabase db = SQLiteDatabase.create(null);
            try {
                db.execSQL("CREATE TABLE t(k TEXT, v INTEGER)");
                db.execSQL("INSERT INTO t VALUES ('a', 1), ('b', 2), ('a', 3)");
                StringBuilder got = new StringBuilder();
                try (Cursor c = db.rawQuery("SELECT k, SUM(v), typeof(SUM(v)), 1.5 FROM t GROUP BY k ORDER BY k", null)) {
                    while (c.moveToNext())
                        got.append(c.getString(0)).append(' ').append(c.getLong(1)).append(' ').append(c.getString(2))
                            .append(' ').append(c.getType(1)).append(' ').append(c.getType(3)).append("; ");
                }
                return Outcome.check("a 4 integer 1 2; b 2 integer 1 2;", got.toString().trim());
            } finally {
                db.close();
            }
        }
    }

    /** Android's org.json keeps the keys in insertion order and escapes the slash. */
    static final class JsonOrder implements Case {
        @Override public Outcome run(Env env) throws Exception {
            JSONObject o = new JSONObject();
            o.put("b", 1);
            o.put("a", "x/y");
            o.put("c", 0.5);
            return Outcome.check("{\"b\":1,\"a\":\"x\\/y\",\"c\":0.5}", o.toString());
        }
    }

    /** With no log.tag property, Log.isLoggable is true from INFO up and false for DEBUG. */
    static final class LogIsLoggable implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("debug false, info true", "debug " + Log.isLoggable("edenprobe", Log.DEBUG) + ", info "
                + Log.isLoggable("edenprobe", Log.INFO));
        }
    }

    /**
     * The runtime's Double.toString and Float.toString: values that older and newer algorithms print apart. ART prints
     * them as OpenJDK did before JDK 19's shortest digits (seen on Android 16), 1.0E23 as 9.999999999999999E22.
     */
    static final class FloatingToString implements Case {
        @Override public Outcome run(Env env) {
            String got = Double.toString(2.0E-3) + " " + Double.toString(2.82879384806159E17) + " " + Double.toString(1.0E23)
                + " " + Float.toString(1.0E10f) + " " + Double.toString(0.1 + 0.2);
            return Outcome.check("0.002 2.82879384806159008E17 9.999999999999999E22 1.0E10 0.30000000000000004", got);
        }
    }
}
