package fr.gcstation.telecommande;

// Globe des radios : rendu OpenGL ES 2.0 (puce graphique du telephone).
//  - Terre : carte Natural Earth (res/drawable-nodpi/terre.png), oceans et terres aux couleurs neon, cotes
//    lumineuses (calculees dans le shader), quadrillage, jour / nuit selon l'heure reelle (soleil au bon endroit),
//    reflet bleu sur le bord ;
//  - halo d'atmosphere autour de la Terre, etoiles ;
//  - radios : points lumineux (couleur = genre, taille = popularite), colonnes de lumiere pour les plus ecoutees ;
//  - anneaux : la radio choisie, celle qui joue sur la GameCube ; arc anime entre les deux.
// Camera : latitude / longitude / distance, avec inertie et vols animes (calcules ici, a chaque image).

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.GLUtils;
import android.opengl.Matrix;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.ShortBuffer;
import java.util.Calendar;
import java.util.List;
import java.util.Random;
import java.util.TimeZone;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

class GlobeRenderer implements GLSurfaceView.Renderer {
    // ---- camera (ecrite par l'interface, lue ici) ----
    volatile float lat = 30, lon = 5, dist = 3.2f;
    volatile float vLat, vLon;                               // inertie (degres / s)
    volatile boolean touching, autoTurn = true;
    private float flyT = -1, flyLat0, flyLon0, flyD0, flyLat1, flyLon1, flyD1;
    private long lastNs;
    private float time;
    volatile long lastTouchMs;

    // ---- radios ----
    volatile List<RadioDir.Station> stations;
    volatile boolean[] visible;                              // (filtre : null = toutes)
    volatile int version;                                    // (change : refaire les tampons)
    private int builtVersion = -1;
    volatile int selected = -1, playing = -1;

    // ---- pour le choix au doigt et l'etiquette (lus par l'interface) ----
    final float[] mvp = new float[16], mv = new float[16];
    volatile int width = 1, height = 1;
    volatile float selX = -1, selY = -1;                     // (ecran ; -1 : cachee)
    interface FrameListener { void onFrame(); }
    volatile FrameListener frameListener;

    private final Context ctx;
    private final float[] skyMv = new float[16], skyMvp = new float[16];
    private final float[] proj = new float[16], view = new float[16], model = new float[16], tmp = new float[16], vp = new float[16];
    private int earthProg, ptProg, lineProg, haloProg;
    private int earthTex;
    private FloatBuffer sphereV;
    private ShortBuffer sphereI;
    private int sphereCount;
    private FloatBuffer starV;
    private int starN;
    private FloatBuffer ptV, pillarV;
    private int ptN, pillarN;
    private final FloatBuffer markV = buf(16), arcV = buf(65 * 7), haloV = buf(8);
    private static final float FOV = 34f;

    GlobeRenderer(Context c) { ctx = c; }

    static FloatBuffer buf(int floats) { return ByteBuffer.allocateDirect(floats * 4).order(ByteOrder.nativeOrder()).asFloatBuffer(); }

    // ---------------- shaders ----------------
    private static final String EARTH_V =
        "attribute vec3 aPos; attribute vec2 aUv; uniform mat4 uMVP; uniform mat4 uMV;" +
        "varying vec2 vUv; varying vec3 vN; varying vec3 vNv;" +
        "void main(){ vUv=aUv; vN=aPos; vNv=normalize((uMV*vec4(aPos,0.0)).xyz); gl_Position=uMVP*vec4(aPos,1.0); }";
    private static final String EARTH_F =
        "precision mediump float; uniform sampler2D uLand; uniform vec3 uSun; uniform float uTex; uniform float uTime;" +
        "varying vec2 vUv; varying vec3 vN; varying vec3 vNv;" +
        "void main(){" +
        " float land=texture2D(uLand,vUv).r;" +
        " float dx=texture2D(uLand,vUv+vec2(uTex,0.0)).r-texture2D(uLand,vUv-vec2(uTex,0.0)).r;" +
        " float dy=texture2D(uLand,vUv+vec2(0.0,uTex*2.0)).r-texture2D(uLand,vUv-vec2(0.0,uTex*2.0)).r;" +
        " float coast=clamp(length(vec2(dx,dy))*1.6,0.0,1.0);" +
        " vec3 n=normalize(vN);" +
        " vec3 ocean=mix(vec3(0.03,0.02,0.11),vec3(0.01,0.05,0.13),abs(n.y));" +
        " vec3 landc=mix(vec3(0.24,0.08,0.38),vec3(0.10,0.17,0.40),smoothstep(-0.6,0.8,n.y));" +
        " vec3 c=mix(ocean,landc,land);" +
        " float la=(0.5-vUv.y)*12.0, lo=vUv.x*24.0;" +                       // quadrillage tous les 15 degres
        " float g=max(1.0-smoothstep(0.0,0.035,abs(fract(la)-0.5)*2.0-0.93),0.0);" +
        " g=max(g,(1.0-smoothstep(0.0,0.035,abs(fract(lo)-0.5)*2.0-0.93))*sqrt(max(1.0-n.y*n.y,0.0)));" +
        " c+=vec3(0.15,0.55,0.85)*g*0.10*(1.0-land*0.5);" +
        " float s=dot(n,uSun); float day=smoothstep(-0.18,0.22,s);" +
        " c*=mix(0.42,1.3,day);" +
        " c+=vec3(1.0,0.18,0.82)*coast*(0.55+0.55*(1.0-day));" +              // cotes neon (plus fortes la nuit)
        " c+=vec3(1.0,0.45,0.25)*(1.0-smoothstep(0.0,0.10,abs(s)))*0.12;" +  // ligne du soir
        " float rim=pow(1.0-max(vNv.z,0.0),2.6); c+=vec3(0.13,0.85,1.0)*rim*0.45;" +
        " gl_FragColor=vec4(c,1.0); }";
    private static final String PT_V =
        "attribute vec3 aPos; attribute vec4 aCol; attribute float aSize; uniform mat4 uMVP; uniform mat4 uMV; uniform float uScale; uniform float uMode;" +
        "varying vec4 vCol;" +
        "void main(){ gl_Position=uMVP*vec4(aPos,1.0); vec3 p=(uMV*vec4(aPos,1.0)).xyz; vec3 n=normalize((uMV*vec4(aPos,0.0)).xyz);" +
        " float f=uMode>1.5?1.0:dot(n,normalize(-p)); vCol=aCol*smoothstep(0.02,0.2,f); gl_PointSize=aSize*uScale; }";
    private static final String PT_F =
        "precision mediump float; varying vec4 vCol; uniform float uMode; uniform float uTime;" +
        "void main(){ float d=length(gl_PointCoord-vec2(0.5))*2.0; if(d>1.0) discard;" +
        " float a; vec3 c=vCol.rgb;" +
        " if(uMode<0.5){ a=pow(1.0-d,1.6); c=mix(c,vec3(1.0),pow(max(1.0-d*2.2,0.0),2.0)); }" +   // point : coeur blanc, halo colore
        " else if(uMode<1.5){ float r=0.62+0.12*sin(uTime*4.0); a=smoothstep(0.10,0.0,abs(d-r))+0.25*smoothstep(0.35,0.0,abs(d-r)); }" +   // anneau
        " else { a=pow(1.0-d,3.0)*0.9; }" +                                       // etoile
        " gl_FragColor=vec4(c*a*vCol.a,a*vCol.a); }";
    private static final String LINE_V =
        "attribute vec3 aPos; attribute vec4 aCol; uniform mat4 uMVP; uniform mat4 uMV; varying vec4 vCol;" +
        "void main(){ gl_Position=uMVP*vec4(aPos,1.0); vec3 p=(uMV*vec4(aPos,1.0)).xyz; vec3 n=normalize((uMV*vec4(normalize(aPos),0.0)).xyz);" +
        " vCol=aCol*smoothstep(-0.05,0.15,dot(n,normalize(-p))); }";
    private static final String LINE_F =
        "precision mediump float; varying vec4 vCol; uniform float uTime; uniform float uDash;" +
        "void main(){ float a=vCol.a; if(uDash>0.5) a*=0.35+0.65*smoothstep(0.4,1.0,fract(vCol.a*8.0-uTime*1.2));" +
        " gl_FragColor=vec4(vCol.rgb*a,a); }";
    private static final String HALO_V =
        "attribute vec2 aPos; varying vec2 vP; uniform vec2 uR; void main(){ vP=aPos; gl_Position=vec4(aPos*uR,0.0,1.0); }";
    private static final String HALO_F =
        "precision mediump float; varying vec2 vP; uniform float uK;" +
        "void main(){ float d=length(vP)*uK; float a=d<1.0?pow(d,8.0)*0.35:exp(-(d-1.0)*9.0);" +
        " vec3 c=mix(vec3(0.13,0.91,1.0),vec3(1.0,0.18,0.82),clamp((vP.y+1.0)*0.5,0.0,1.0)*0.6);" +
        " gl_FragColor=vec4(c*a*0.9,a*0.9); }";

    private static int shader(int type, String src) {
        int s = GLES20.glCreateShader(type);
        GLES20.glShaderSource(s, src);
        GLES20.glCompileShader(s);
        return s;
    }
    private static int program(String v, String f) {
        int p = GLES20.glCreateProgram();
        GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, v));
        GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, f));
        GLES20.glLinkProgram(p);
        return p;
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig cfg) {
        GLES20.glClearColor(0.016f, 0.004f, 0.04f, 1f);
        earthProg = program(EARTH_V, EARTH_F);
        ptProg = program(PT_V, PT_F);
        lineProg = program(LINE_V, LINE_F);
        haloProg = program(HALO_V, HALO_F);
        buildSphere(72, 144);
        buildStars();
        haloV.position(0);
        haloV.put(new float[] { -1, -1, 1, -1, -1, 1, 1, 1 });
        // texture de la Terre
        int[] t = new int[1];
        GLES20.glGenTextures(1, t, 0);
        earthTex = t[0];
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, earthTex);
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inScaled = false;
        Bitmap b = BitmapFactory.decodeResource(ctx.getResources(), R.drawable.terre, o);
        if (b != null) { GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, b, 0); b.recycle(); }
        GLES20.glGenerateMipmap(GLES20.GL_TEXTURE_2D);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR_MIPMAP_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_REPEAT);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        builtVersion = -1;
        lastNs = 0;
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int w, int h) {
        GLES20.glViewport(0, 0, w, h);
        width = w; height = h;
        Matrix.perspectiveM(proj, 0, FOV, (float) w / Math.max(1, h), 0.05f, 100f);
    }

    // ---------------- geometrie ----------------
    static void toXYZ(float lat, float lon, float r, float[] o, int k) {
        double la = Math.toRadians(lat), lo = Math.toRadians(lon);
        o[k] = (float) (Math.cos(la) * Math.sin(lo)) * r;
        o[k + 1] = (float) Math.sin(la) * r;
        o[k + 2] = (float) (Math.cos(la) * Math.cos(lo)) * r;
    }

    private void buildSphere(int nLat, int nLon) {
        float[] v = new float[(nLat + 1) * (nLon + 1) * 5];
        int k = 0;
        for (int i = 0; i <= nLat; i++) {
            float la = 90f - 180f * i / nLat;
            for (int j = 0; j <= nLon; j++) {
                float lo = -180f + 360f * j / nLon;
                toXYZ(la, lo, 1f, v, k);
                v[k + 3] = (float) j / nLon;
                v[k + 4] = (float) i / nLat;
                k += 5;
            }
        }
        short[] idx = new short[nLat * nLon * 6];
        int n = 0;
        for (int i = 0; i < nLat; i++) for (int j = 0; j < nLon; j++) {
            int a = i * (nLon + 1) + j, b = a + nLon + 1;
            idx[n++] = (short) a; idx[n++] = (short) b; idx[n++] = (short) (a + 1);
            idx[n++] = (short) (a + 1); idx[n++] = (short) b; idx[n++] = (short) (b + 1);
        }
        sphereV = buf(v.length); sphereV.put(v).position(0);
        sphereI = ByteBuffer.allocateDirect(idx.length * 2).order(ByteOrder.nativeOrder()).asShortBuffer();
        sphereI.put(idx).position(0);
        sphereCount = idx.length;
    }

    private void buildStars() {
        Random r = new Random(11);
        starN = 900;
        float[] v = new float[starN * 8];
        for (int i = 0; i < starN; i++) {
            float la = (float) Math.toDegrees(Math.asin(r.nextFloat() * 2 - 1)), lo = r.nextFloat() * 360 - 180;
            toXYZ(la, lo, 40f, v, i * 8);
            float b = 0.35f + r.nextFloat() * 0.65f, tint = r.nextFloat();
            v[i * 8 + 3] = b * (tint < 0.2f ? 1f : 0.85f); v[i * 8 + 4] = b * 0.85f; v[i * 8 + 5] = b * (tint > 0.7f ? 1f : 0.9f); v[i * 8 + 6] = b;
            v[i * 8 + 7] = 1.5f + r.nextFloat() * r.nextFloat() * 4.5f;
        }
        starV = buf(v.length); starV.put(v).position(0);
    }

    /** points et colonnes de lumiere des radios (refaits quand la liste ou le filtre change) */
    private void buildStations() {
        List<RadioDir.Station> st = stations;
        boolean[] vis = visible;
        builtVersion = version;
        if (st == null) { ptN = pillarN = 0; return; }
        int n = st.size();
        float[] v = new float[n * 8];
        int maxClicks = 1;
        for (RadioDir.Station s : st) if (s.clicks > maxClicks) maxClicks = s.clicks;
        double lm = Math.log(1 + maxClicks);
        int k = 0, np = 0;
        float[] pl = new float[Math.min(n, 400) * 2 * 7];
        int pk = 0;
        float[] xyz = new float[3];
        for (int i = 0; i < n; i++) {
            if (vis != null && (i >= vis.length || !vis[i])) continue;
            RadioDir.Station s = st.get(i);
            toXYZ(s.lat, s.lon, 1.004f, v, k);
            int c = RadioDir.GENRE_COL[s.genre];
            float pop = (float) (Math.log(1 + s.clicks) / lm);
            v[k + 3] = ((c >> 16) & 255) / 255f; v[k + 4] = ((c >> 8) & 255) / 255f; v[k + 5] = (c & 255) / 255f;
            v[k + 6] = 0.55f + 0.45f * pop;
            v[k + 7] = 5f + 13f * pop;
            k += 8;
            if (np < 400 && pop > 0.62f) {                      // colonnes : les plus ecoutees
                float h = 1.004f + 0.02f + 0.16f * (pop - 0.62f) / 0.38f;
                toXYZ(s.lat, s.lon, 1.004f, xyz, 0);
                pl[pk++] = xyz[0]; pl[pk++] = xyz[1]; pl[pk++] = xyz[2];
                pl[pk++] = v[k - 5]; pl[pk++] = v[k - 4]; pl[pk++] = v[k - 3]; pl[pk++] = 0.9f;
                toXYZ(s.lat, s.lon, h, xyz, 0);
                pl[pk++] = xyz[0]; pl[pk++] = xyz[1]; pl[pk++] = xyz[2];
                pl[pk++] = v[k - 5]; pl[pk++] = v[k - 4]; pl[pk++] = v[k - 3]; pl[pk++] = 0f;
                np++;
            }
        }
        ptN = k / 8;
        ptV = buf(Math.max(8, k)); ptV.put(v, 0, k).position(0);
        pillarN = np * 2;
        pillarV = buf(Math.max(7, pk)); pillarV.put(pl, 0, pk).position(0);
    }

    // ---------------- camera ----------------
    void flyTo(float la, float lo, float d) {
        flyLat0 = lat; flyLon0 = lon; flyD0 = dist;
        float dl = lo - lon;
        while (dl > 180) dl -= 360;
        while (dl < -180) dl += 360;
        flyLat1 = la; flyLon1 = lon + dl; flyD1 = d;
        flyT = 0;
        vLat = vLon = 0;
    }
    boolean flying() { return flyT >= 0; }

    private void step(float dt) {
        if (flyT >= 0) {
            flyT += dt / 1.3f;
            float t = Math.min(1f, flyT), e = t < 0.5f ? 4 * t * t * t : 1 - (float) Math.pow(-2 * t + 2, 3) / 2;
            float lift = (float) Math.sin(Math.PI * t) * Math.min(1.2f, Math.abs(flyLon1 - flyLon0) / 120f + Math.abs(flyLat1 - flyLat0) / 90f);
            lat = flyLat0 + (flyLat1 - flyLat0) * e;
            lon = flyLon0 + (flyLon1 - flyLon0) * e;
            dist = flyD0 + (flyD1 - flyD0) * e + lift;          // (on prend de la hauteur pour les grands vols)
            if (t >= 1) flyT = -1;
        } else if (!touching) {
            lon += vLon * dt; lat += vLat * dt;
            float k = (float) Math.exp(-dt * 2.6);
            vLon *= k; vLat *= k;
            if (autoTurn && Math.abs(vLon) < 0.5f && System.currentTimeMillis() - lastTouchMs > 6000) lon += dt * 3f;   // (tourne doucement)
        }
        if (lat > 85) lat = 85;
        if (lat < -85) lat = -85;
        if (lon > 180) lon -= 360;
        if (lon < -180) lon += 360;
    }

    /** direction du soleil (repere de la Terre) : declinaison du jour, longitude selon l'heure UTC */
    private static void sun(float[] o) {
        Calendar c = Calendar.getInstance(TimeZone.getTimeZone("UTC"));
        int day = c.get(Calendar.DAY_OF_YEAR);
        float hours = c.get(Calendar.HOUR_OF_DAY) + c.get(Calendar.MINUTE) / 60f;
        float decl = (float) (-23.44 * Math.cos(2 * Math.PI / 365 * (day + 10)));
        float slon = -15f * (hours - 12f);
        toXYZ(decl, slon, 1f, o, 0);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        long now = System.nanoTime();
        float dt = lastNs == 0 ? 0.016f : Math.min(0.05f, (now - lastNs) / 1e9f);
        lastNs = now;
        time += dt;
        step(dt);
        if (builtVersion != version) buildStations();

        Matrix.setLookAtM(view, 0, 0, 0, dist, 0, 0, 0, 0, 1, 0);
        Matrix.setIdentityM(model, 0);
        Matrix.rotateM(model, 0, lat, 1, 0, 0);
        Matrix.rotateM(model, 0, -lon, 0, 1, 0);
        Matrix.multiplyMM(mv, 0, view, 0, model, 0);
        Matrix.multiplyMM(mvp, 0, proj, 0, mv, 0);
        Matrix.multiplyMM(vp, 0, proj, 0, view, 0);

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT);
        float zoom = (3.2f / dist);
        float dens = ctx.getResources().getDisplayMetrics().density;

        // etoiles (ciel fixe, tourne avec la Terre)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST);
        GLES20.glEnable(GLES20.GL_BLEND);
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE);
        Matrix.setLookAtM(tmp, 0, 0, 0, 0.001f, 0, 0, 0, 0, 1, 0);
        Matrix.multiplyMM(skyMv, 0, tmp, 0, model, 0);
        Matrix.multiplyMM(skyMvp, 0, proj, 0, skyMv, 0);
        drawPoints(starV, starN, skyMvp, skyMv, dens * 0.8f, 2f, true);

        // halo d'atmosphere (carre a l'ecran autour de la Terre)
        double ang = Math.asin(1.0 / dist);
        float rs = (float) (Math.tan(ang) / Math.tan(Math.toRadians(FOV / 2)));
        float aspect = (float) width / Math.max(1, height);
        GLES20.glUseProgram(haloProg);
        GLES20.glUniform2f(GLES20.glGetUniformLocation(haloProg, "uR"), rs * 1.35f / aspect, rs * 1.35f);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(haloProg, "uK"), 1.35f);
        int hp = GLES20.glGetAttribLocation(haloProg, "aPos");
        haloV.position(0);
        GLES20.glVertexAttribPointer(hp, 2, GLES20.GL_FLOAT, false, 8, haloV);
        GLES20.glEnableVertexAttribArray(hp);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        GLES20.glDisableVertexAttribArray(hp);

        // la Terre
        GLES20.glDisable(GLES20.GL_BLEND);
        GLES20.glEnable(GLES20.GL_DEPTH_TEST);
        GLES20.glUseProgram(earthProg);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(earthProg, "uMVP"), 1, false, mvp, 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(earthProg, "uMV"), 1, false, mv, 0);
        float[] s = new float[3];
        sun(s);
        GLES20.glUniform3f(GLES20.glGetUniformLocation(earthProg, "uSun"), s[0], s[1], s[2]);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(earthProg, "uTex"), 1f / 2048f * (dist > 2.2f ? 1.6f : 1f));
        GLES20.glUniform1f(GLES20.glGetUniformLocation(earthProg, "uTime"), time);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, earthTex);
        GLES20.glUniform1i(GLES20.glGetUniformLocation(earthProg, "uLand"), 0);
        int ap = GLES20.glGetAttribLocation(earthProg, "aPos"), au = GLES20.glGetAttribLocation(earthProg, "aUv");
        sphereV.position(0);
        GLES20.glVertexAttribPointer(ap, 3, GLES20.GL_FLOAT, false, 20, sphereV);
        sphereV.position(3);
        GLES20.glVertexAttribPointer(au, 2, GLES20.GL_FLOAT, false, 20, sphereV);
        GLES20.glEnableVertexAttribArray(ap);
        GLES20.glEnableVertexAttribArray(au);
        sphereI.position(0);
        GLES20.glDrawElements(GLES20.GL_TRIANGLES, sphereCount, GLES20.GL_UNSIGNED_SHORT, sphereI);
        GLES20.glDisableVertexAttribArray(ap);
        GLES20.glDisableVertexAttribArray(au);

        // radios : colonnes puis points (lumiere qui s'ajoute)
        GLES20.glEnable(GLES20.GL_BLEND);
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA);
        GLES20.glDepthMask(false);
        if (pillarN > 0) drawLines(pillarV, pillarN, GLES20.GL_LINES, false, 2f * dens);
        GLES20.glDisable(GLES20.GL_DEPTH_TEST);
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE);
        if (ptN > 0) drawPoints(ptV, ptN, mvp, mv, dens * (0.55f + 0.45f * zoom), 0f, false);

        // anneaux et arc
        List<RadioDir.Station> st = stations;
        int sel = selected, play = playing;
        markV.position(0);
        int mk = 0;
        float[] p = new float[3];
        if (st != null && sel >= 0 && sel < st.size()) { mk += mark(st.get(sel), 0xFFFFFFFF, 46, p); }
        if (st != null && play >= 0 && play < st.size() && play != sel) { mk += mark(st.get(play), Neon.CYAN, 40, p); }
        if (mk > 0) { markV.position(0); drawPoints(markV, mk, mvp, mv, dens * (0.7f + 0.3f * zoom), 1f, false); }
        if (st != null && sel >= 0 && play >= 0 && sel != play && sel < st.size() && play < st.size()) arc(st.get(play), st.get(sel), dens);
        GLES20.glDepthMask(true);

        // etiquette de la radio choisie (position a l'ecran, pour l'interface)
        if (st != null && sel >= 0 && sel < st.size()) {
            RadioDir.Station x = st.get(sel);
            float[] w = new float[4], c = new float[4], e = new float[4];
            toXYZ(x.lat, x.lon, 1.004f, w, 0);
            w[3] = 1;
            Matrix.multiplyMV(c, 0, mvp, 0, w, 0);
            Matrix.multiplyMV(e, 0, mv, 0, w, 0);
            float[] nn = new float[4];
            w[3] = 0;
            Matrix.multiplyMV(nn, 0, mv, 0, w, 0);
            boolean front = -(nn[0] * e[0] + nn[1] * e[1] + nn[2] * e[2]) > 0;
            if (front && c[3] > 0) { selX = (c[0] / c[3] + 1) / 2 * width; selY = (1 - c[1] / c[3]) / 2 * height; }
            else selX = selY = -1;
        } else selX = selY = -1;
        FrameListener fl = frameListener;
        if (fl != null) fl.onFrame();
    }

    private int mark(RadioDir.Station s, int col, float size, float[] p) {
        toXYZ(s.lat, s.lon, 1.006f, p, 0);
        markV.put(p[0]).put(p[1]).put(p[2]);
        markV.put(((col >> 16) & 255) / 255f).put(((col >> 8) & 255) / 255f).put((col & 255) / 255f).put(1f);
        markV.put(size);
        return 1;
    }

    /** arc en grand cercle (radio de la console -> radio choisie), en pointilles qui avancent */
    private void arc(RadioDir.Station a, RadioDir.Station b, float dens) {
        float[] pa = new float[3], pb = new float[3];
        toXYZ(a.lat, a.lon, 1f, pa, 0);
        toXYZ(b.lat, b.lon, 1f, pb, 0);
        double dot = Math.max(-1, Math.min(1, pa[0] * pb[0] + pa[1] * pb[1] + pa[2] * pb[2])), om = Math.acos(dot);
        if (om < 1e-3) return;
        arcV.position(0);
        int n = 64;
        for (int i = 0; i <= n; i++) {
            double t = (double) i / n, s0 = Math.sin((1 - t) * om) / Math.sin(om), s1 = Math.sin(t * om) / Math.sin(om);
            float h = 1.004f + (float) (Math.sin(Math.PI * t) * Math.min(0.35, om * 0.22));
            float x = (float) (pa[0] * s0 + pb[0] * s1), y = (float) (pa[1] * s0 + pb[1] * s1), z = (float) (pa[2] * s0 + pb[2] * s1);
            float l = (float) Math.sqrt(x * x + y * y + z * z);
            arcV.put(x / l * h).put(y / l * h).put(z / l * h);
            float tt = (float) t;
            arcV.put(0.13f + 0.87f * tt).put(0.91f - 0.73f * tt).put(1f - 0.18f * tt).put(tt);   // (alpha porte la position : pointilles)
        }
        arcV.position(0);
        drawLines(arcV, n + 1, GLES20.GL_LINE_STRIP, true, 3f * dens);
    }

    private void drawPoints(FloatBuffer v, int n, float[] m, float[] mvm, float scale, float mode, boolean stars) {
        GLES20.glUseProgram(ptProg);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(ptProg, "uMVP"), 1, false, m, 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(ptProg, "uMV"), 1, false, mvm, 0);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(ptProg, "uScale"), scale);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(ptProg, "uMode"), mode);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(ptProg, "uTime"), time);
        int ap = GLES20.glGetAttribLocation(ptProg, "aPos"), ac = GLES20.glGetAttribLocation(ptProg, "aCol"), as = GLES20.glGetAttribLocation(ptProg, "aSize");
        v.position(0);
        GLES20.glVertexAttribPointer(ap, 3, GLES20.GL_FLOAT, false, 32, v);
        v.position(3);
        GLES20.glVertexAttribPointer(ac, 4, GLES20.GL_FLOAT, false, 32, v);
        v.position(7);
        GLES20.glVertexAttribPointer(as, 1, GLES20.GL_FLOAT, false, 32, v);
        GLES20.glEnableVertexAttribArray(ap);
        GLES20.glEnableVertexAttribArray(ac);
        GLES20.glEnableVertexAttribArray(as);
        GLES20.glDrawArrays(GLES20.GL_POINTS, 0, n);
        GLES20.glDisableVertexAttribArray(ap);
        GLES20.glDisableVertexAttribArray(ac);
        GLES20.glDisableVertexAttribArray(as);
        v.position(0);
    }

    private void drawLines(FloatBuffer v, int n, int mode, boolean dash, float width) {
        GLES20.glUseProgram(lineProg);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(lineProg, "uMVP"), 1, false, mvp, 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(lineProg, "uMV"), 1, false, mv, 0);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(lineProg, "uTime"), time);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(lineProg, "uDash"), dash ? 1f : 0f);
        GLES20.glLineWidth(width);
        int ap = GLES20.glGetAttribLocation(lineProg, "aPos"), ac = GLES20.glGetAttribLocation(lineProg, "aCol");
        v.position(0);
        GLES20.glVertexAttribPointer(ap, 3, GLES20.GL_FLOAT, false, 28, v);
        v.position(3);
        GLES20.glVertexAttribPointer(ac, 4, GLES20.GL_FLOAT, false, 28, v);
        GLES20.glEnableVertexAttribArray(ap);
        GLES20.glEnableVertexAttribArray(ac);
        GLES20.glDrawArrays(mode, 0, n);
        GLES20.glDisableVertexAttribArray(ap);
        GLES20.glDisableVertexAttribArray(ac);
        v.position(0);
    }

    /** la radio la plus proche du doigt (ecran), parmi celles de face et visibles ; -1 si aucune (fil de l'interface) */
    int pick(float x, float y, float radiusPx) {
        List<RadioDir.Station> st = stations;
        boolean[] vis = visible;
        if (st == null) return -1;
        float[] m = mvp.clone(), e = mv.clone();
        int w = width, h = height, best = -1;
        float bd = radiusPx * radiusPx, bestScore = Float.MAX_VALUE;
        float[] p = new float[3];
        for (int i = 0; i < st.size(); i++) {
            if (vis != null && (i >= vis.length || !vis[i])) continue;
            RadioDir.Station s = st.get(i);
            toXYZ(s.lat, s.lon, 1.004f, p, 0);
            float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            if (cw <= 0) continue;
            float cx = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw, cy = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
            float sx = (cx + 1) / 2 * w, sy = (1 - cy) / 2 * h, dx = sx - x, dy = sy - y, d = dx * dx + dy * dy;
            if (d > bd) continue;
            float ex = e[0] * p[0] + e[4] * p[1] + e[8] * p[2] + e[12], ey = e[1] * p[0] + e[5] * p[1] + e[9] * p[2] + e[13], ez = e[2] * p[0] + e[6] * p[1] + e[10] * p[2] + e[14];
            float nx = e[0] * p[0] + e[4] * p[1] + e[8] * p[2], ny = e[1] * p[0] + e[5] * p[1] + e[9] * p[2], nz = e[2] * p[0] + e[6] * p[1] + e[10] * p[2];
            if (-(nx * ex + ny * ey + nz * ez) <= 0) continue;   // (de l'autre cote de la Terre)
            float score = d / (1f + 0.25f * (float) Math.log(1 + s.clicks));   // (la plus proche ; a egalite, la plus ecoutee)
            if (score < bestScore) { bestScore = score; best = i; }
        }
        return best;
    }
}
