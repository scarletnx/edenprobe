// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.widget.FrameLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The launcher activity: it runs the probe once per process and finishes when the run ends. The report is in the
 * app's external files, edenprobe/runs/BUILD/report.jsonl, which adb pulls on a device (docs/Android.md); every line
 * also goes to logcat under the tag edenprobe, the last one EDENPROBE DONE with the counts. A launch with the string
 * extra "mode" writes those words to the mode file first, the one way a script sets them on a device whose shell
 * cannot write the app's folder.
 */
public final class ProbeActivity extends Activity {
    private static final AtomicBoolean started = new AtomicBoolean();

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout root = new FrameLayout(this);
        TextView status = new TextView(this);
        status.setText(R.string.running);
        root.addView(status);
        setContentView(root);
        // a run per process: an activity made again (a rotation) leaves the running one alone
        if (!started.compareAndSet(false, true)) return;
        Env env = new Env(this, root);
        File files = getExternalFilesDir(null);
        if (files == null) {
            // the one folder adb and the layer both read: without it a report would go where nobody looks for it
            Log.e("edenprobe", "the run did not start: getExternalFilesDir(null) is null, no external storage");
            status.setText(R.string.no_storage);
            return;
        }
        File folder = new File(files, "edenprobe");
        String probe = folder.getPath();
        String words = getIntent().getStringExtra("mode");
        if (words != null) {
            File mode = new File(folder, "mode");
            try {
                if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("cannot make " + folder);
                try (FileOutputStream out = new FileOutputStream(mode)) {
                    out.write((words + "\n").getBytes(StandardCharsets.UTF_8));
                }
            } catch (IOException e) {
                // the words asked for are not the ones a run would read: no run rather than another one
                Log.e("edenprobe", "the run did not start: the mode \"" + words + "\" was not written to " + mode + ": "
                    + Outcome.describe(e));
                status.setText(R.string.no_mode);
                return;
            }
        }
        new Thread(() -> {
            try {
                String device = Build.MANUFACTURER + " " + Build.MODEL + ", Android " + Build.VERSION.RELEASE + " (API "
                    + Build.VERSION.SDK_INT + "), ABIs " + Arrays.toString(Build.SUPPORTED_ABIS);
                int failed = Engine.run(env, probe, getCacheDir().getPath(), device);
                runOnUiThread(() -> status.setText(getString(R.string.done, failed)));
            } catch (Throwable t) {
                Log.e("edenprobe", "the run stopped: " + Outcome.describe(t), t);
            } finally {
                runOnUiThread(this::finish);
            }
        }, "edenprobe").start();
    }
}
