// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import android.app.Activity;
import android.os.Bundle;
import android.widget.FrameLayout;
import android.widget.TextView;
import java.util.concurrent.atomic.AtomicBoolean;

/** The launcher activity: it shows the run's progress and finishes when the run ends (on the layer, the process ends). */
public final class ProbeActivity extends Activity {
    private static final AtomicBoolean started = new AtomicBoolean();

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout root = new FrameLayout(this);
        TextView status = new TextView(this);
        status.setText("androidprobe");
        root.addView(status);
        setContentView(root);
        // a run per process: an activity made again (a rotation) leaves the running one alone
        if (started.compareAndSet(false, true)) new Thread(new Probe(this, root, status), "androidprobe").start();
    }
}
