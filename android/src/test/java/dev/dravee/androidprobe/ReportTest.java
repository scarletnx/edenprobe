// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class ReportTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    static final Def A = new Def("s.a", "s", "case a", "exact", "x.A"), B = new Def("s.b", "s", "case b", "exact", "x.B"),
        C = new Def("s.c", "s", "case c", "none", "x.C");

    List<String> lines(Report r) throws Exception {
        return Files.readAllLines(r.report.toPath(), StandardCharsets.UTF_8);
    }

    @Test public void aCaseLineHasEdenprobesFieldsThenOurs() throws Exception {
        Report r = new Report(tmp.newFolder("run"));
        r.launch();
        r.start(A);
        String line = r.result(A, Outcome.check("1", "2"), 42);
        assertEquals("{\"type\":\"case\",\"suite\":\"s\",\"name\":\"case a\",\"tier\":\"exact\",\"result\":\"fail\","
            + "\"detail\":\"got 2, expected 1\",\"id\":\"s.a\",\"got\":\"2\",\"want\":\"1\",\"us\":42}", line);
        String skip = r.result(B, Outcome.skip("needs API 30", "API 26"), 0);
        assertTrue(skip, skip.contains("\"result\":\"skip\",\"reason\":\"needs API 30\",\"detail\":\"API 26\""));
    }

    @Test public void aLaunchThatEndedInACaseGoesOnAfterIt() throws Exception {
        File dir = tmp.newFolder("run");
        Report first = new Report(dir);
        assertEquals(1, first.launch());
        first.note("starting");
        first.start(A);
        first.result(A, Outcome.pass("x", "x"), 1);
        first.start(B); // the process ends here

        Report second = new Report(dir);
        assertFalse(second.complete());
        assertEquals(1, second.launches());
        assertTrue(second.done("s.a"));
        assertFalse(second.done("s.b"));
        assertEquals("s.b", second.unfinished());
        assertEquals(2, second.launch());
        assertTrue(second.crashed(B, 1).contains("\"result\":\"crashed\",\"detail\":\"launch 1 ended during this case"));
        assertTrue(second.done("s.b"));
        second.start(C);
        second.result(C, Outcome.fail("no", "no", null), 3);
        assertEquals("{\"type\":\"summary\",\"passed\":1,\"failed\":1,\"skipped\":0,\"crashed\":1}", second.summary());
        assertTrue(second.complete());

        Report third = new Report(dir);
        assertTrue(third.complete());
        assertNull(third.unfinished());
        List<String> l = lines(third);
        assertEquals(5, l.size());
        assertTrue(l.get(4).startsWith("{\"type\":\"summary\""));
    }

    @Test public void aCaseThatEndedCleanlyLeavesNothingUnfinished() throws Exception {
        File dir = tmp.newFolder("run");
        Report r = new Report(dir);
        r.launch();
        r.start(A);
        r.result(A, Outcome.pass("x", "x"), 1);
        assertNull(new Report(dir).unfinished());
    }

    @Test public void threwNamesTheExceptionAndItsCause() {
        Outcome o = Outcome.threw(new IllegalStateException("outer", new java.io.IOException("inner")));
        assertEquals("fail", o.result);
        assertEquals("threw java.lang.IllegalStateException: outer (caused by java.io.IOException: inner)", o.got);
    }
}
