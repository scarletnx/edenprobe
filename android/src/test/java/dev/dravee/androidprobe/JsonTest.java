// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

public class JsonTest {
    @Test public void escapesWhatJsonNeedsAndKeepsTheRest() {
        assertEquals("\"a\\\"b\\\\c\\n\\t\\r\\u0001\\u007f ç 😀\"", Json.quote("a\"b\\c\n\t\r\u0001\u007f ç 😀"));
    }

    @Test public void aLoneSurrogateIsItsCode() {
        assertEquals("\"x\\ud83dy\\ude00\"", Json.quote("x\ud83dy\ude00"));
    }

    @Test public void keysKeepTheirOrderAndNullIsNull() {
        assertEquals("{\"type\":\"case\",\"n\":-3,\"s\":null}", new Json().put("type", "case").put("n", -3).put("s", (String) null).toString());
    }

    @Test public void fieldReadsBackWhatQuoteWrote() {
        String text = "a\"b\\c\n\u0001ç";
        String line = new Json().put("type", "note").put("text", text).toString();
        assertEquals(text, Json.field(line, "text"));
        assertEquals("note", Json.field(line, "type"));
        assertNull(Json.field(line, "missing"));
    }
}
