// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import java.io.File;
import java.io.IOException;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/** What a case runs with: the activity, its root view, the main thread, and a scratch folder of its own. */
final class Env {
    static final long UI_TIMEOUT_MS = 10000;
    final Activity activity;
    final ViewGroup root;
    private final Handler main = new Handler(Looper.getMainLooper());

    Env(Activity activity, ViewGroup root) {
        this.activity = activity;
        this.root = root;
    }

    /**
     * Runs the call on the main thread and waits for it, UI_TIMEOUT_MS at most; what it throws, this throws, an Error
     * included (a class the framework lacks), so the main thread's loop never sees it.
     */
    <T> T onUi(Callable<T> call) throws Exception {
        final Object[] value = new Object[1];
        final Throwable[] error = new Throwable[1];
        final CountDownLatch done = new CountDownLatch(1);
        main.post(() -> {
            try {
                value[0] = call.call();
            } catch (Throwable t) {
                error[0] = t;
            } finally {
                done.countDown();
            }
        });
        if (!done.await(UI_TIMEOUT_MS, TimeUnit.MILLISECONDS))
            throw new TimeoutException("the main thread did not run the call in " + UI_TIMEOUT_MS + " ms");
        if (error[0] instanceof Exception) throw (Exception) error[0];
        if (error[0] instanceof Error) throw (Error) error[0];
        if (error[0] != null) throw new IllegalStateException(error[0]);
        @SuppressWarnings("unchecked") T t = (T) value[0];
        return t;
    }

    /** An empty folder for one case, under the app's files: whatever the folder held before is deleted. */
    File scratch(String name) throws IOException {
        File dir = new File(new File(activity.getFilesDir(), "scratch"), name);
        delete(dir);
        if (!dir.mkdirs()) throw new IOException("cannot make the scratch folder " + dir);
        return dir;
    }

    private static void delete(File f) throws IOException {
        File[] children = f.listFiles();
        if (children != null)
            for (File c : children) delete(c);
        if (f.exists() && !f.delete()) throw new IOException("cannot delete " + f);
    }
}
