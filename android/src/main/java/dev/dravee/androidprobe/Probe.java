// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import android.app.Activity;
import android.os.Build;
import android.system.Os;
import android.system.OsConstants;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.File;
import java.util.Arrays;

/**
 * A run: every case not yet reported, in Cases' order, each line synced before the next case starts. The report is in
 * the app's external files, runs/REVISION/report.jsonl, which adb pulls on a device and the layer keeps in its data
 * folder. Logcat (tag androidprobe) gets each line too, and "done: PATH" when the summary is written.
 */
final class Probe implements Runnable {
    static final String TAG = "androidprobe";
    private final Activity activity;
    private final Env env;
    private final TextView status;

    Probe(Activity activity, ViewGroup root, TextView status) {
        this.activity = activity;
        this.env = new Env(activity, root);
        this.status = status;
    }

    @Override public void run() {
        try {
            File dir = new File(activity.getExternalFilesDir(null), "runs/" + BuildConfig.REVISION);
            Log.i(TAG, "run folder: " + dir);
            Report report = new Report(dir);
            if (report.complete()) {
                show("This run is complete: " + report.report + ". Clear the app's data to run it again.");
                Log.i(TAG, "done: " + report.report + " (complete before this launch)");
                return;
            }
            int launch = report.launch();
            if (launch == 1) {
                report.note("androidprobe " + BuildConfig.VERSION_NAME + " (" + BuildConfig.REVISION + ") starting");
                report.note("folder: " + dir);
                report.note("mode: every case, " + Cases.ALL.size() + " of them");
                report.note(device());
            } else {
                report.note("launch " + launch + ": going on after the cases the earlier launches reported");
            }
            String unfinished = report.unfinished();
            if (unfinished != null) {
                Def d = Cases.find(unfinished);
                if (d == null) throw new IllegalStateException("the journal names a case this build does not have: " + unfinished);
                Log.w(TAG, report.crashed(d, launch - 1));
            }
            int index = 0;
            for (Def d : Cases.ALL) {
                ++index;
                if (report.done(d.id)) continue;
                show("case " + index + " of " + Cases.ALL.size() + ": " + d.id);
                report.start(d);
                long t0 = System.nanoTime();
                Outcome o;
                try {
                    Case c = (Case) Class.forName(d.className).getDeclaredConstructor().newInstance();
                    o = c.run(env);
                } catch (Throwable t) {
                    o = Outcome.threw(t instanceof java.lang.reflect.InvocationTargetException && t.getCause() != null ? t.getCause() : t);
                }
                Log.i(TAG, report.result(d, o, (System.nanoTime() - t0) / 1000));
            }
            report.note("journal: this run took " + launch + (launch == 1 ? " launch" : " launches"));
            String summary = report.summary();
            Log.i(TAG, summary);
            show("Done: " + summary);
            Log.i(TAG, "done: " + report.report);
        } catch (Throwable t) {
            Log.e(TAG, "the run stopped: " + Outcome.describe(t), t);
            show("The run stopped: " + Outcome.describe(t));
        } finally {
            activity.runOnUiThread(activity::finish);
        }
    }

    private String device() {
        String abi;
        try {
            NativeLib.require();
            abi = NativeLib.abi();
        } catch (Throwable t) {
            abi = "not loaded (" + Outcome.describe(t) + ")";
        }
        long page = Os.sysconf(OsConstants._SC_PAGESIZE);
        return "device: " + Build.MANUFACTURER + " " + Build.MODEL + ", Android " + Build.VERSION.RELEASE + " (API "
            + Build.VERSION.SDK_INT + "), ABIs " + Arrays.toString(Build.SUPPORTED_ABIS) + ", native library " + abi
            + ", page size " + page;
    }

    private void show(String text) {
        activity.runOnUiThread(() -> status.setText(text));
    }
}
