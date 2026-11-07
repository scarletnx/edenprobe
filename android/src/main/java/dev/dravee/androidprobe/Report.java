// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * A run's folder: report.jsonl in edenprobe's format (notes, one line per case, the summary last) and the journal,
 * which names each launch and the case each one started. Every line is synced before the case after it starts, so a
 * case that ends the process (a native crash, a kill on a timeout) leaves a report that the next launch goes on from:
 * that case is reported "crashed" and the run goes on after it.
 */
final class Report {
    final File dir, report, journal;
    private final Set<String> done = new HashSet<>();
    private final List<String> started = new ArrayList<>();
    private int launches;
    private boolean complete;

    Report(File dir) throws IOException {
        this.dir = dir;
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("cannot make the run folder " + dir);
        report = new File(dir, "report.jsonl");
        journal = new File(dir, "journal");
        for (String line : lines(report)) {
            if (line.contains("\"type\":\"summary\"")) complete = true;
            String id = "case".equals(Json.field(line, "type")) ? Json.field(line, "id") : null;
            if (id != null) done.add(id);
        }
        for (String line : lines(journal)) {
            if (line.equals("launch")) ++launches;
            else if (line.startsWith("start ")) started.add(line.substring(6));
        }
    }

    /** The run has its summary: nothing is left to run. */
    boolean complete() {
        return complete;
    }

    /** The case has its line in the report. */
    boolean done(String id) {
        return done.contains(id);
    }

    /** The launches before this one. */
    int launches() {
        return launches;
    }

    /** The case that the last launch started and never reported (it ended the process), or null. */
    String unfinished() {
        if (started.isEmpty()) return null;
        String last = started.get(started.size() - 1);
        return done.contains(last) ? null : last;
    }

    /** This launch, in the journal; its number. */
    int launch() throws IOException {
        append(journal, "launch");
        return ++launches;
    }

    void note(String text) throws IOException {
        append(report, new Json().put("type", "note").put("text", text).toString());
    }

    /** The journal's mark that the case starts: should the process end before its line, the next launch knows it. */
    void start(Def def) throws IOException {
        append(journal, "start " + def.id);
        started.add(def.id);
    }

    /** The case's line; us is its time in microseconds. Returns the line. */
    String result(Def def, Outcome o, long us) throws IOException {
        Json j = new Json().put("type", "case").put("suite", def.suite).put("name", def.name).put("tier", def.tier)
            .put("result", o.result);
        if (o.reason != null) j.put("reason", o.reason);
        j.put("detail", o.detail).put("id", def.id);
        if (o.got != null) j.put("got", o.got);
        if (o.want != null) j.put("want", o.want);
        if (us >= 0) j.put("us", us);
        String line = j.toString();
        append(report, line);
        done.add(def.id);
        return line;
    }

    /** The line of a case whose launch ended in it: launch is that launch's number. */
    String crashed(Def def, int launch) throws IOException {
        String detail = "launch " + launch + " ended during this case (a crash, or a kill on a timeout); the run went on "
            + "after it";
        Json j = new Json().put("type", "case").put("suite", def.suite).put("name", def.name).put("tier", def.tier)
            .put("result", "crashed").put("detail", detail).put("id", def.id);
        String line = j.toString();
        append(report, line);
        done.add(def.id);
        return line;
    }

    /** The counts of every case line in the report, as edenprobe's summary has them; written last. */
    String summary() throws IOException {
        long passed = 0, failed = 0, skipped = 0, crashed = 0;
        for (String line : lines(report)) {
            if (!"case".equals(Json.field(line, "type"))) continue;
            String r = Json.field(line, "result");
            if ("pass".equals(r)) ++passed;
            else if ("fail".equals(r)) ++failed;
            else if ("skip".equals(r)) ++skipped;
            else if ("crashed".equals(r)) ++crashed;
            else throw new IOException("a case line with the result " + r + ": " + line);
        }
        String line = new Json().put("type", "summary").put("passed", passed).put("failed", failed)
            .put("skipped", skipped).put("crashed", crashed).toString();
        append(report, line);
        complete = true;
        return line;
    }

    private static List<String> lines(File f) throws IOException {
        List<String> out = new ArrayList<>();
        if (!f.exists()) return out;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            for (String line; (line = r.readLine()) != null;)
                if (!line.isEmpty()) out.add(line);
        }
        return out;
    }

    private static void append(File f, String line) throws IOException {
        try (FileOutputStream out = new FileOutputStream(f, true)) {
            out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
    }
}
