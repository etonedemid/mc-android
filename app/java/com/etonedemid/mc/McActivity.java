package com.etonedemid.mc;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// The game's activity: NativeActivity (all of the game is native) plus the one thing native code can't do on its own,
// a dialog. Before the game starts it asks: sign in with Microsoft, skip, or skip and don't ask again. Signing in is
// the device-code flow (a code to enter at microsoft.com/link), then Xbox Live, XSTS and the Minecraft services; the
// result goes to the native side, which starts the game with that name, UUID and token. The refresh token is kept, so
// later launches sign in without the dialog.
public class McActivity extends NativeActivity {
    static { System.loadLibrary("mc"); }   // so the native method below resolves (NativeActivity loads it by path)

    // name, uuid, token == null: play offline
    static native void nativeLogin(String name, String uuid, String token);

    // Azure application (client) ID with public client flows enabled and Minecraft API access approved by Mojang;
    // set at build time (link-apk.sh writes it into Config.java). Empty: sign-in not configured.
    static final String CLIENT_ID = Config.MSA_CLIENT_ID;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private SharedPreferences prefs;
    private boolean decided;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (saved != null) return;   // recreated: the game is already running with its account
        prefs = getSharedPreferences("account", Context.MODE_PRIVATE);
        String refresh = prefs.getString("refresh_token", null);
        if (refresh != null && !CLIENT_ID.isEmpty()) { silentSignIn(refresh); return; }
        if (prefs.getBoolean("never_ask", false)) { offline(); return; }
        ask();
    }

    private void ask() {
        new AlertDialog.Builder(this)
            .setTitle("Minecraft account")
            .setMessage("Sign in with Microsoft to play on online servers. Without signing in you can play singleplayer "
                + "and offline-mode servers.")
            .setCancelable(false)
            .setPositiveButton("Sign in", (d, w) -> signIn())
            .setNegativeButton("Skip", (d, w) -> offline())
            .setNeutralButton("Don't ask again", (d, w) -> { prefs.edit().putBoolean("never_ask", true).apply(); offline(); })
            .show();
    }

    private void offline() { finish(null, null, null); }
    private void finish(String name, String uuid, String token) {
        if (decided) return;
        decided = true;
        nativeLogin(name, uuid, token);
    }
    private void fail(String what) {
        ui.post(() -> new AlertDialog.Builder(this).setTitle("Sign-in failed").setMessage(what).setCancelable(false)
            .setPositiveButton("Play offline", (d, w) -> offline())
            .setNeutralButton("Try again", (d, w) -> ask()).show());
    }

    // --- device code flow ---
    private void signIn() {
        if (CLIENT_ID.isEmpty()) {
            new AlertDialog.Builder(this).setTitle("Sign-in not set up")
                .setMessage("This build has no Microsoft application ID (MSA_CLIENT_ID in link-apk.sh). Playing offline.")
                .setCancelable(false).setPositiveButton("OK", (d, w) -> offline()).show();
            return;
        }
        new Thread(() -> {
            try {
                String r = post("https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode",
                    "client_id=" + enc(CLIENT_ID) + "&scope=" + enc("XboxLive.signin offline_access"));
                String code = str(r, "user_code"), device = str(r, "device_code");
                int interval = num(r, "interval", 5), expires = num(r, "expires_in", 900);
                AlertDialog[] waiting = new AlertDialog[1];
                ui.post(() -> waiting[0] = new AlertDialog.Builder(this).setTitle("Sign in")
                    .setMessage("Go to microsoft.com/link and enter\n\n" + code + "\n\nThe game starts once you have signed in.")
                    .setCancelable(false)
                    .setPositiveButton("Open the page", null)
                    .setNegativeButton("Cancel", (d, w) -> offline())
                    .show());
                // "Open the page" keeps the dialog up (no listener above): the browser opens over it
                ui.post(() -> waiting[0].getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v ->
                    startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://www.microsoft.com/link?otc=" + code)))));
                long end = System.currentTimeMillis() + expires * 1000L;
                while (!decided && System.currentTimeMillis() < end) {
                    Thread.sleep(interval * 1000L);
                    String t = postAllowError("https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
                        "grant_type=" + enc("urn:ietf:params:oauth:grant-type:device_code") + "&client_id=" + enc(CLIENT_ID)
                        + "&device_code=" + enc(device));
                    String err = str(t, "error");
                    if ("authorization_pending".equals(err)) continue;
                    if ("slow_down".equals(err)) { interval += 5; continue; }
                    ui.post(() -> waiting[0].dismiss());
                    if (err != null) { fail("Microsoft: " + err); return; }
                    minecraft(str(t, "access_token"), str(t, "refresh_token"));
                    return;
                }
                if (!decided) { ui.post(() -> waiting[0].dismiss()); fail("The code expired."); }
            } catch (Exception e) { fail(e.toString()); }
        }).start();
    }

    private void silentSignIn(String refresh) {
        new Thread(() -> {
            try {
                String t = post("https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
                    "grant_type=refresh_token&client_id=" + enc(CLIENT_ID) + "&scope=" + enc("XboxLive.signin offline_access")
                    + "&refresh_token=" + enc(refresh));
                minecraft(str(t, "access_token"), str(t, "refresh_token"));
            } catch (Exception e) {
                prefs.edit().remove("refresh_token").apply();
                ui.post(this::ask);   // the saved sign-in no longer works: ask again
            }
        }).start();
    }

    // Microsoft token -> Xbox Live -> XSTS -> Minecraft token -> profile (also proves the account owns the game)
    private void minecraft(String msToken, String refresh) {
        try {
            String xbl = postJson("https://user.auth.xboxlive.com/user/authenticate",
                "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"d=" + msToken
                + "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}", null);
            String xblToken = str(xbl, "Token"), uhs = str(xbl, "uhs");
            String xsts = postJson("https://xsts.auth.xboxlive.com/xsts/authorize",
                "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" + xblToken
                + "\"]},\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}", null);
            String mc = postJson("https://api.minecraftservices.com/authentication/login_with_xbox",
                "{\"identityToken\":\"XBL3.0 x=" + uhs + ";" + str(xsts, "Token") + "\"}", null);
            String token = str(mc, "access_token");
            String profile = get("https://api.minecraftservices.com/minecraft/profile", token);
            String name = str(profile, "name"), id = str(profile, "id");
            if (name == null || id == null) { fail("This Microsoft account doesn't own Minecraft: Java Edition."); return; }
            if (refresh != null) prefs.edit().putString("refresh_token", refresh).apply();
            ui.post(() -> finish(name, id, token));
        } catch (Exception e) { fail(e.toString()); }
    }

    // --- tiny HTTP + JSON helpers (flat lookups are enough for these responses) ---
    private static String enc(String s) throws Exception { return URLEncoder.encode(s, "UTF-8"); }
    private static String post(String url, String form) throws Exception { return send(url, "application/x-www-form-urlencoded", form, null, false); }
    private static String postAllowError(String url, String form) throws Exception { return send(url, "application/x-www-form-urlencoded", form, null, true); }
    private static String postJson(String url, String json, String bearer) throws Exception { return send(url, "application/json", json, bearer, false); }
    private static String get(String url, String bearer) throws Exception { return send(url, null, null, bearer, false); }
    private static String send(String url, String type, String body, String bearer, boolean allowError) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(15000); c.setReadTimeout(15000);
        c.setRequestProperty("Accept", "application/json");
        if (bearer != null) c.setRequestProperty("Authorization", "Bearer " + bearer);
        if (body != null) {
            c.setDoOutput(true); c.setRequestMethod("POST"); c.setRequestProperty("Content-Type", type);
            try (OutputStream o = c.getOutputStream()) { o.write(body.getBytes("UTF-8")); }
        }
        int code = c.getResponseCode();
        InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        if (in != null) { byte[] b = new byte[4096]; int n; while ((n = in.read(b)) > 0) out.write(b, 0, n); in.close(); }
        String r = out.toString("UTF-8");
        if (code >= 400 && !allowError) throw new Exception(url.replaceAll("https://([^/]+)/.*", "$1") + " " + code + " " + r);
        return r;
    }
    private static String str(String json, String key) {
        Matcher m = Pattern.compile("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"").matcher(json);
        return m.find() ? m.group(1) : null;
    }
    private static int num(String json, String key, int dflt) {
        Matcher m = Pattern.compile("\"" + key + "\"\\s*:\\s*(\\d+)").matcher(json);
        return m.find() ? Integer.parseInt(m.group(1)) : dflt;
    }
}
