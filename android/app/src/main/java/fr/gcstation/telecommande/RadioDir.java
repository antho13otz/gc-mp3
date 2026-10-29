package fr.gcstation.telecommande;

// Globe des radios : l'annuaire mondial radio-browser.info (libre), lu directement par le telephone.
// Seulement les radios placees sur la carte (has_geo_info), les plus ecoutees d'abord, par pages de 2 500 ;
// gardees 7 jours dans un fichier de l'appli (le globe s'ouvre tout de suite, sans reseau).

import android.content.Context;
import android.util.JsonReader;
import android.util.JsonToken;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.Normalizer;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class RadioDir {
    static final class Station {
        String uuid, name, url, cc, country, state, tags, codec;
        float lat, lon;
        int bitrate, clicks, votes, genre;
        boolean hls;
        String key;                                         // (texte de recherche : minuscules, sans accents)
        /** la GameCube sait la lire ? (MP3, AAC, OGG Vorbis ; pas HLS, Opus, FLAC) */
        boolean gcOk() {
            String c = codec == null ? "" : codec.toUpperCase(Locale.ROOT), u = url == null ? "" : url.toLowerCase(Locale.ROOT);
            if (hls || u.contains(".m3u8") || u.contains("opus") || u.contains("flac")) return false;
            return c.equals("MP3") || c.startsWith("AAC") || c.equals("OGG") || c.isEmpty();
        }
    }

    // genres : nom, mots des etiquettes, couleur (neon)
    static final String[] GENRE = { "Pop", "Rock", "Jazz", "Classique", "Électro", "Info", "Hip-hop", "Rétro", "Lounge", "Monde", "Autres" };
    static final int[] GENRE_COL = { 0xFFFF4FD8, 0xFFFF6A3D, 0xFFFFC23D, 0xFFE8F0FF, 0xFF21E8FF, 0xFF5CFF8A, 0xFFB46BFF, 0xFFFFE45C, 0xFF7FB8FF, 0xFFFF9A6B, 0xFF8C7CFF };
    private static final String[][] GENRE_WORDS = {
        { "pop", "hits", "top 40", "chart", "variet" },
        { "rock", "metal", "punk", "grunge", "indie", "alternative" },
        { "jazz", "blues", "swing", "soul", "funk" },
        { "classical", "classique", "opera", "baroque", "symph", "piano" },
        { "electro", "dance", "house", "techno", "trance", "edm", "dubstep", "drum and bass", "dnb", "club" },
        { "news", "talk", "info", "sport", "politic", "actualit", "public radio" },
        { "hip", "rap", "rnb", "r&b", "urban", "trap" },
        { "oldies", "80s", "70s", "60s", "90s", "retro", "classic hits", "nostalg" },
        { "lounge", "chill", "ambient", "relax", "easy listening", "smooth", "downtempo" },
        { "world", "latin", "reggae", "africa", "folk", "traditional", "salsa", "kpop", "bollywood", "arab" },
    };
    private static final String[] SERVERS = { "de1.api.radio-browser.info", "fi1.api.radio-browser.info", "nl1.api.radio-browser.info", "at1.api.radio-browser.info", "de2.api.radio-browser.info" };
    static final int PAGE = 2500, PAGES = 4;                 // (10 000 radios au plus)
    private static final long MAX_AGE = 7L * 24 * 3600 * 1000;

    private RadioDir() { }

    static int genreOf(String tags) {
        String t = tags == null ? "" : tags.toLowerCase(Locale.ROOT);
        for (int g = 0; g < GENRE_WORDS.length; g++) for (String w : GENRE_WORDS[g]) if (t.contains(w)) return g;
        return GENRE.length - 1;
    }

    static String fold(String s) {
        if (s == null) return "";
        String n = Normalizer.normalize(s, Normalizer.Form.NFD).replaceAll("\\p{M}+", "");
        return n.toLowerCase(Locale.ROOT);
    }

    /** drapeau du pays (lettres regionales) */
    static String flag(String cc) {
        if (cc == null || cc.length() != 2) return "";
        String u = cc.toUpperCase(Locale.ROOT);
        if (u.charAt(0) < 'A' || u.charAt(0) > 'Z' || u.charAt(1) < 'A' || u.charAt(1) > 'Z') return "";
        return new String(Character.toChars(0x1F1E6 + u.charAt(0) - 'A')) + new String(Character.toChars(0x1F1E6 + u.charAt(1) - 'A'));
    }

    private static void finish(Station s) {
        s.genre = genreOf(s.tags);
        s.key = fold(s.name + " " + s.country + " " + s.state + " " + s.tags);
    }

    // ---------- cache (fichier de l'appli) ----------
    private static File cacheFile(Context c) { return new File(c.getFilesDir(), "radios-globe.tsv"); }
    private static String clean(String s) { return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' '); }

    static boolean cacheFresh(Context c) {
        File f = cacheFile(c);
        return f.exists() && System.currentTimeMillis() - f.lastModified() < MAX_AGE;
    }

    static List<Station> loadCache(Context c) {
        List<Station> r = new ArrayList<>();
        File f = cacheFile(c);
        if (!f.exists()) return r;
        try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"))) {
            String l;
            while ((l = in.readLine()) != null) {
                String[] p = l.split("\t", -1);
                if (p.length < 14) continue;
                Station s = new Station();
                s.uuid = p[0]; s.name = p[1]; s.url = p[2];
                try { s.lat = Float.parseFloat(p[3]); s.lon = Float.parseFloat(p[4]); } catch (Exception e) { continue; }
                s.cc = p[5]; s.country = p[6]; s.state = p[7]; s.tags = p[8]; s.codec = p[9];
                s.bitrate = parse(p[10]); s.clicks = parse(p[11]); s.votes = parse(p[12]); s.hls = "1".equals(p[13]);
                finish(s);
                r.add(s);
            }
        } catch (Exception e) { }
        return r;
    }

    static void saveCache(Context c, List<Station> list) {
        File tmp = new File(c.getFilesDir(), "radios-globe.tmp");
        try (BufferedWriter w = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(tmp), "UTF-8"))) {
            for (Station s : list) {
                w.write(clean(s.uuid) + "\t" + clean(s.name) + "\t" + clean(s.url) + "\t" + s.lat + "\t" + s.lon + "\t" + clean(s.cc) + "\t" + clean(s.country)
                        + "\t" + clean(s.state) + "\t" + clean(s.tags) + "\t" + clean(s.codec) + "\t" + s.bitrate + "\t" + s.clicks + "\t" + s.votes + "\t" + (s.hls ? 1 : 0) + "\n");
            }
        } catch (Exception e) { tmp.delete(); return; }
        File f = cacheFile(c);
        f.delete();
        tmp.renameTo(f);
    }

    private static int parse(String s) { try { return Integer.parseInt(s.trim()); } catch (Exception e) { return 0; } }

    // ---------- reseau ----------
    interface Progress { boolean page(List<Station> got, int page); }   // (false : arreter)

    /** telecharge l'annuaire (fil de fond) ; chaque page arrive par progress ; null si aucun serveur ne repond */
    static List<Station> fetch(Progress progress) {
        List<Station> all = new ArrayList<>();
        java.util.HashSet<String> seen = new java.util.HashSet<>();
        int srv = 0;
        for (int page = 0; page < PAGES; page++) {
            List<Station> got = null;
            for (int tries = 0; tries < SERVERS.length && got == null; tries++) {
                got = fetchPage(SERVERS[srv], page * PAGE);
                if (got == null) srv = (srv + 1) % SERVERS.length;
            }
            if (got == null) return all.isEmpty() ? null : all;
            for (Station s : got) if (seen.add(s.url.toLowerCase(Locale.ROOT))) all.add(s);
            if (progress != null && !progress.page(all, page)) break;
            if (got.size() < PAGE / 2) break;
        }
        return all;
    }

    private static List<Station> fetchPage(String host, int offset) {
        HttpURLConnection c = null;
        try {
            URL u = new URL("https://" + host + "/json/stations/search?has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=" + PAGE + "&offset=" + offset);
            c = (HttpURLConnection) u.openConnection();
            c.setConnectTimeout(8000);
            c.setReadTimeout(30000);
            c.setRequestProperty("User-Agent", "GCStation-Telecommande/1.0");
            if (c.getResponseCode() != 200) return null;
            InputStream in = c.getInputStream();
            List<Station> r = new ArrayList<>();
            JsonReader j = new JsonReader(new InputStreamReader(in, "UTF-8"));
            j.beginArray();
            while (j.hasNext()) {
                Station s = new Station();
                String url = "", urlRes = "";
                boolean geo = false, geoLon = false;
                j.beginObject();
                while (j.hasNext()) {
                    String k = j.nextName();
                    if (j.peek() == JsonToken.NULL) { j.nextNull(); continue; }
                    switch (k) {
                        case "stationuuid": s.uuid = j.nextString(); break;
                        case "name": s.name = j.nextString().trim(); break;
                        case "url": url = j.nextString(); break;
                        case "url_resolved": urlRes = j.nextString(); break;
                        case "countrycode": s.cc = j.nextString(); break;
                        case "country": s.country = j.nextString(); break;
                        case "state": s.state = j.nextString(); break;
                        case "tags": s.tags = j.nextString(); break;
                        case "codec": s.codec = j.nextString(); break;
                        case "bitrate": s.bitrate = j.nextInt(); break;
                        case "clickcount": s.clicks = j.nextInt(); break;
                        case "votes": s.votes = j.nextInt(); break;
                        case "hls": s.hls = j.nextInt() != 0; break;
                        case "geo_lat": s.lat = (float) j.nextDouble(); geo = true; break;
                        case "geo_long": s.lon = (float) j.nextDouble(); geoLon = true; break;
                        default: j.skipValue();
                    }
                }
                j.endObject();
                s.url = !urlRes.isEmpty() ? urlRes : url;
                if (!geo || !geoLon || s.url == null || !s.url.startsWith("http") || s.name == null || s.name.isEmpty()) continue;
                if (Math.abs(s.lat) > 90 || Math.abs(s.lon) > 180 || (s.lat == 0 && s.lon == 0)) continue;
                if (s.cc == null) s.cc = "";
                if (s.country == null) s.country = "";
                if (s.state == null) s.state = "";
                if (s.tags == null) s.tags = "";
                if (s.codec == null) s.codec = "";
                if (s.uuid == null) s.uuid = "";
                finish(s);
                r.add(s);
            }
            j.endArray();
            j.close();
            return r;
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    /** signale l'ecoute a l'annuaire (comme le demandent ses regles) ; fil de fond */
    static void click(String uuid) {
        if (uuid == null || uuid.isEmpty()) return;
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("https://" + SERVERS[0] + "/json/url/" + uuid).openConnection();
            c.setConnectTimeout(5000);
            c.setReadTimeout(5000);
            c.setRequestProperty("User-Agent", "GCStation-Telecommande/1.0");
            c.getResponseCode();
        } catch (Exception e) {
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
