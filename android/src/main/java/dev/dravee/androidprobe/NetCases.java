// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.ConnectException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** The network as an app sees it with or without a connection: loopback, a refused port, names that never resolve. */
final class NetCases {
    private NetCases() {}

    static final int TIMEOUT_MS = 5000;

    /** A port nothing listens on: one the kernel gave a server socket that is closed again. */
    static int closedPort() throws Exception {
        try (ServerSocket s = new ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))) {
            return s.getLocalPort();
        }
    }

    static final class JavaTcpLoopback implements Case {
        @Override public Outcome run(Env env) throws Exception {
            try (ServerSocket server = new ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))) {
                server.setSoTimeout(TIMEOUT_MS);
                Thread t = new Thread(() -> {
                    try (Socket s = server.accept()) {
                        String line = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8)).readLine();
                        OutputStream out = s.getOutputStream();
                        out.write(("pong:" + line + "\n").getBytes(StandardCharsets.UTF_8));
                        out.flush();
                    } catch (Exception ignored) {
                        // the client's read times out and reports it
                    }
                }, "androidprobe-echo");
                t.start();
                try (Socket c = new Socket()) {
                    c.connect(new InetSocketAddress("127.0.0.1", server.getLocalPort()), TIMEOUT_MS);
                    c.setSoTimeout(TIMEOUT_MS);
                    c.getOutputStream().write("ping\n".getBytes(StandardCharsets.UTF_8));
                    String got = new BufferedReader(new InputStreamReader(c.getInputStream(), StandardCharsets.UTF_8)).readLine();
                    t.join(TIMEOUT_MS);
                    return Outcome.check("pong:ping", got);
                }
            }
        }
    }

    static final class NativeTcpLoopback implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("listening on a port, accepted, abc", NativeLib.tcpLoopback());
        }
    }

    /** A connection to a closed port on loopback: ConnectException in Java, ECONNREFUSED (111) in C. */
    static final class ConnectRefused implements Case {
        @Override public Outcome run(Env env) throws Exception {
            int port = closedPort();
            String java;
            try (Socket c = new Socket()) {
                c.connect(new InetSocketAddress("127.0.0.1", port), TIMEOUT_MS);
                java = "connected";
            } catch (ConnectException e) {
                java = e.getClass().getName();
            }
            NativeLib.require();
            return Outcome.check("java.net.ConnectException|-1 errno 111", java + "|" + NativeLib.connectRefused(closedPort()));
        }
    }

    /** RFC 6761's .invalid never resolves, online or not: UnknownHostException, and getaddrinfo's EAI_NODATA (7). */
    static final class InvalidName implements Case {
        @Override public Outcome run(Env env) {
            String java;
            try {
                java = "resolved " + InetAddress.getByName("androidprobe.invalid");
            } catch (UnknownHostException e) {
                java = e.getClass().getName();
            }
            NativeLib.require();
            return Outcome.check("java.net.UnknownHostException|" + "getaddrinfo 7", java + "|" + NativeLib.resolve("androidprobe.invalid"));
        }
    }

    /** localhost is the hosts file's: Android's names 127.0.0.1 alone (::1 is ip6-localhost). */
    static final class Localhost implements Case {
        @Override public Outcome run(Env env) throws Exception {
            List<String> java = new ArrayList<>();
            for (InetAddress a : InetAddress.getAllByName("localhost")) java.add(a.getHostAddress());
            NativeLib.require();
            return Outcome.check("java [127.0.0.1]|native 127.0.0.1", "java " + java + "|native " + NativeLib.resolve("localhost"));
        }
    }
}
